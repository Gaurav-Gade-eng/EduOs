#include "idt.h"
#include "gdt.h"

typedef struct { uint16_t off_low, sel; uint8_t zero, type_attr; uint16_t off_high; } __attribute__((packed)) idt_entry_t;
typedef struct { uint16_t limit; uint32_t base; } __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idtr;

extern uint32_t isr_stub_table[48];
extern uint32_t isr_syscall_stub;

static void set_gate(int v, uint32_t handler, uint8_t type_attr) {
    idt[v].off_low = handler & 0xFFFF;
    idt[v].sel = GDT_KERNEL_CODE;
    idt[v].zero = 0;
    idt[v].type_attr = type_attr;
    idt[v].off_high = (handler >> 16) & 0xFFFF;
}

void idt_init(void) {
    for (int i = 0; i < 48; i++) set_gate(i, isr_stub_table[i], 0x8E);   /* ring-0 interrupt gate */
    set_gate(VECTOR_SYSCALL, isr_syscall_stub, 0x8E);                    /* ring 0 only (no user mode yet) */
    idtr.limit = sizeof idt - 1;
    idtr.base = (uint32_t)(uintptr_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}
