/* Kernel entry and initialization sequence (see spec section 7.4). */
#include "boot_info.h"
#include "config.h"
#include "console.h"
#include "serial.h"
#include "telemetry.h"
#include "gdt.h"
#include "idt.h"
#include "interrupts.h"
#include "memory.h"
#include "paging.h"
#include "timer.h"
#include "keyboard.h"
#include "process.h"
#include "scheduler.h"
#include "syscall.h"
#include "ata.h"
#include "filesystem.h"
#include "shell.h"
#include "panic.h"
#include "io.h"

extern char __kernel_start[], __kernel_end[];

static void worker_a(void);
static void worker_b(void);

static void ready(const char *name) {
    telemetry_emit_module_ready(name, NULL);
    telemetry_flush();
}

static void worker_body(void) {
    uint32_t last = timer_get_ticks();
    volatile uint32_t counter = 0;
    for (;;) {
        counter++;
        if (timer_get_ticks() - last >= 500) {
            last = timer_get_ticks();
            sys_yield();
        }
    }
}
static void worker_a(void) { worker_body(); }
static void worker_b(void) { worker_body(); }

static void periodic_snapshots(void) {
    memory_emit_snapshot();
    scheduler_emit_snapshot();
    process_emit_snapshots();
    telemetry_emit_interrupt_count(0, irq_count(0));
    telemetry_emit_interrupt_count(1, irq_count(1));
    telemetry_emit_stats();
}

void kernel_main(boot_info_t *bi) {
    serial_init();
    telemetry_init();
    console_init();
    console_write("EduOS kernel entered\n");

    if (!bi || bi->magic != EDUOS_BOOT_MAGIC) {
        console_write("bad boot_info magic\n");
        kernel_panic("BAD_BOOT_INFO", "boot_info magic mismatch");
    }
    console_printf("kernel image 0x%08x..0x%08x\n", (uint32_t)(uintptr_t)__kernel_start, (uint32_t)(uintptr_t)__kernel_end);

    telemetry_emit_boot(EDUOS_VERSION, EDUOS_ARCH);
    ready("Kernel_Core");
    ready("Serial_Telemetry");

    gdt_init();  ready("GDT");
    idt_init();  ready("IDT");
    pic_init();  ready("Interrupt_Controller");

    memory_init(bi);
    if (!memory_map_was_valid()) telemetry_emit_info("E820_UNAVAILABLE", "assuming_fixed_layout");
    ready("Physical_Memory_Manager");

    paging_init(); ready("Paging");

    timer_init(TIMER_HZ);    ready("Timer");
    keyboard_init();         ready("Keyboard");
    process_init();          ready("Process_Manager");
    scheduler_init();        ready("Scheduler");
    syscall_init();          ready("System_Call_Interface");

    if (ata_init() == E_OK) {
        ready("ATA_Driver");
        int rc = fs_mount();
        if (rc == E_OK) ready("EduFS");
        else telemetry_emit_module_error("EduFS", rc == E_NOFS ? "NOT_FORMATTED" : err_name(rc));
    } else {
        telemetry_emit_module_error("ATA_Driver", "NO_DISK");
        telemetry_emit_module_error("EduFS", "NO_DISK");
    }
    telemetry_flush();

    process_create("worker-a", worker_a, 2);
    process_create("worker-b", worker_b, 2);
    shell_init();
    ready("Shell");

    telemetry_emit_kernel_ready();
    telemetry_flush();
    cpu_sti();                                   /* interrupts on: preemption starts */

    uint32_t next_snapshot = timer_get_ticks() + SNAPSHOT_INTERVAL_TICKS;
    for (;;) {
        shell_poll();
        scheduler_drain_events();
        if ((int32_t)(timer_get_ticks() - next_snapshot) >= 0) {
            next_snapshot += SNAPSHOT_INTERVAL_TICKS;
            periodic_snapshots();
        }
        telemetry_flush();
        cpu_hlt();
    }
}
