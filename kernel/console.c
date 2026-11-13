/* VGA text-mode console (80x25 at 0xB8000). */
#include "console.h"
#include "io.h"

#define VGA_MEM ((volatile uint16_t *)0xB8000)
#define COLS 80
#define ROWS 25

static int cur_x, cur_y;
static uint8_t attr = 0x07;

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(cur_y * COLS + cur_x);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}

static void scroll(void) {
    for (int i = 0; i < (ROWS - 1) * COLS; i++) VGA_MEM[i] = VGA_MEM[i + COLS];
    for (int i = (ROWS - 1) * COLS; i < ROWS * COLS; i++) VGA_MEM[i] = (uint16_t)(attr << 8) | ' ';
    cur_y = ROWS - 1;
}

void console_set_color(uint8_t fg, uint8_t bg) { attr = (uint8_t)((bg << 4) | (fg & 0x0F)); }

void console_clear(void) {
    for (int i = 0; i < ROWS * COLS; i++) VGA_MEM[i] = (uint16_t)(attr << 8) | ' ';
    cur_x = cur_y = 0;
    update_cursor();
}

void console_init(void) { console_clear(); }

static void putc_nocursor(char c) {
    if (c == '\n') { cur_x = 0; cur_y++; }
    else if (c == '\r') { cur_x = 0; }
    else if (c == '\t') { cur_x = (cur_x + 8) & ~7; }
    else {
        VGA_MEM[cur_y * COLS + cur_x] = (uint16_t)(attr << 8) | (uint8_t)c;
        cur_x++;
    }
    if (cur_x >= COLS) { cur_x = 0; cur_y++; }
    if (cur_y >= ROWS) scroll();
}

void console_putchar(char c) {
    uint32_t f = irq_save();
    putc_nocursor(c);
    update_cursor();
    irq_restore(f);
}

void console_backspace(void) {
    uint32_t f = irq_save();
    if (cur_x > 0) cur_x--;
    else if (cur_y > 0) { cur_y--; cur_x = COLS - 1; }
    VGA_MEM[cur_y * COLS + cur_x] = (uint16_t)(attr << 8) | ' ';
    update_cursor();
    irq_restore(f);
}

void console_write(const char *text) {
    uint32_t f = irq_save();
    while (*text) putc_nocursor(*text++);
    update_cursor();
    irq_restore(f);
}

void console_write_uint(unsigned int value) {
    char buf[12];
    ksnprintf(buf, sizeof buf, "%u", value);
    console_write(buf);
}

void console_printf(const char *format, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, format);
    kvsnprintf(buf, sizeof buf, format, ap);
    va_end(ap);
    console_write(buf);
}
