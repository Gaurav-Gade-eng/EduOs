#ifndef EDUOS_CONSOLE_H
#define EDUOS_CONSOLE_H
#include "common.h"

void console_init(void);
void console_clear(void);
void console_putchar(char c);
void console_write(const char *text);
void console_write_uint(unsigned int value);
void console_printf(const char *format, ...);
void console_set_color(uint8_t fg, uint8_t bg);
void console_backspace(void);

#endif
