#include "gdt.h"

typedef struct { uint16_t limit_low, base_low; uint8_t base_mid, access, gran, base_high; } __attribute__((packed)) gdt_entry_t;
typedef struct { uint16_t limit; uint32_t base; } __attribute__((packed)) gdt_ptr_t;

static gdt_entry_t gdt[3];
static gdt_ptr_t gdtr;

extern void gdt_flush(void *gdtr);

static void set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].base_high = (base >> 24) & 0xFF;
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].gran = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access = access;
}

void gdt_init(void) {
    set(0, 0, 0, 0, 0);
    set(1, 0, 0xFFFFF, 0x9A, 0xCF);   /* 0x08 kernel code */
    set(2, 0, 0xFFFFF, 0x92, 0xCF);   /* 0x10 kernel data */
    gdtr.limit = sizeof gdt - 1;
    gdtr.base = (uint32_t)(uintptr_t)gdt;
    gdt_flush(&gdtr);
}
