#ifndef EDUOS_SERIAL_H
#define EDUOS_SERIAL_H
#include "common.h"

void serial_init(void);
void serial_write_char(char c);
void serial_write(const char *text);

#endif
