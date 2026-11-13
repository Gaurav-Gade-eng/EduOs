/* Central C-level interrupt/exception dispatcher. */
#include "interrupts.h"
#include "paging.h"
#include "syscall.h"
#include "panic.h"
#include "console.h"
#include "idt.h"

static irq_handler_t irq_handlers[16];
static uint32_t irq_counts[16];

static const char *exc_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range", "Invalid opcode",
    "Device not available", "Double fault", "Coprocessor overrun", "Invalid TSS", "Segment not present",
    "Stack fault", "General protection fault", "Page fault", "Reserved", "x87 FP error",
    "Alignment check", "Machine check", "SIMD FP", "Virtualization", "Control protection",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Security", "Reserved"
};

void irq_register(uint8_t irq, irq_handler_t handler) { if (irq < 16) irq_handlers[irq] = handler; }
uint32_t irq_count(uint8_t irq) { return irq < 16 ? irq_counts[irq] : 0; }

void interrupt_dispatch(interrupt_frame_t *f) {
    if (f->int_no < 32) {
        if (f->int_no == 14 && page_fault_handler(f)) return;
        console_printf("\nEXCEPTION %u: %s  err=0x%x eip=0x%08x\n", f->int_no, exc_names[f->int_no],
                       f->err_code, f->eip);
        kernel_panic("CPU_EXCEPTION", exc_names[f->int_no]);
    }
    if (f->int_no >= 32 && f->int_no < 48) {
        uint8_t irq = (uint8_t)(f->int_no - 32);
        irq_counts[irq]++;
        pic_send_eoi(irq);               /* EOI first: the handler may switch tasks */
        if (irq_handlers[irq]) irq_handlers[irq]();
        return;
    }
    if (f->int_no == VECTOR_SYSCALL) { syscall_dispatch(f); return; }
}
