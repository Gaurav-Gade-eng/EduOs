#include "interrupts.h"
#include "io.h"

#define PIC1 0x20
#define PIC2 0xA0

void pic_init(void) {
    outb(PIC1, 0x11); io_wait();            /* ICW1: init + ICW4 */
    outb(PIC2, 0x11); io_wait();
    outb(PIC1 + 1, 0x20); io_wait();        /* master offset 32 */
    outb(PIC2 + 1, 0x28); io_wait();        /* slave offset 40 */
    outb(PIC1 + 1, 0x04); io_wait();        /* slave on IRQ2 */
    outb(PIC2 + 1, 0x02); io_wait();
    outb(PIC1 + 1, 0x01); io_wait();        /* 8086 mode */
    outb(PIC2 + 1, 0x01); io_wait();
    outb(PIC1 + 1, 0xFF);                   /* mask everything; drivers unmask */
    outb(PIC2 + 1, 0xFF);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2, 0x20);
    outb(PIC1, 0x20);
}

void pic_unmask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1 + 1 : PIC2 + 1;
    uint8_t bit = irq < 8 ? irq : (uint8_t)(irq - 8);
    outb(port, inb(port) & (uint8_t)~(1u << bit));
    if (irq >= 8) outb(PIC1 + 1, inb(PIC1 + 1) & (uint8_t)~(1u << 2));
}
