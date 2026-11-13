#ifndef EDUOS_IO_H
#define EDUOS_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void outw(uint16_t port, uint16_t v) { __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port)); }
static inline uint16_t inw(uint16_t port) { uint16_t v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void io_wait(void) { outb(0x80, 0); }

static inline void insw(uint16_t port, void *addr, uint32_t count) {
    __asm__ volatile("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}
static inline void outsw(uint16_t port, const void *addr, uint32_t count) {
    __asm__ volatile("rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}

static inline void cpu_cli(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void cpu_sti(void) { __asm__ volatile("sti" ::: "memory"); }
static inline void cpu_hlt(void) { __asm__ volatile("hlt" ::: "memory"); }

/* Save EFLAGS and disable interrupts; restore with irq_restore(). */
static inline uint32_t irq_save(void) {
    uint32_t f;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) : : "memory");
    return f;
}
static inline void irq_restore(uint32_t f) {
    __asm__ volatile("pushl %0; popfl" : : "r"(f) : "memory", "cc");
}

#endif
