/* COM1 UART driver (115200 8N1), polled, with bounded waits. */
#include "serial.h"
#include "io.h"

#define COM1 0x3F8

static bool serial_ok;

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* no UART interrupts */
    outb(COM1 + 3, 0x80);   /* DLAB on */
    outb(COM1 + 0, 0x01);   /* divisor 1 -> 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8N1, DLAB off */
    outb(COM1 + 2, 0xC7);   /* enable + clear FIFOs */
    outb(COM1 + 4, 0x0B);   /* DTR, RTS, OUT2 */
    serial_ok = true;
}

void serial_write_char(char c) {
    if (!serial_ok) return;
    for (uint32_t spin = 0; spin < 200000; spin++) {
        if (inb(COM1 + 5) & 0x20) { outb(COM1, (uint8_t)c); return; }
    }
    /* transmitter stuck: drop the byte rather than hang the kernel */
}

void serial_write(const char *text) {
    while (*text) serial_write_char(*text++);
}
