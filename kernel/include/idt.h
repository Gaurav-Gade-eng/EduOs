#ifndef EDUOS_IDT_H
#define EDUOS_IDT_H
#include "common.h"

#define IDT_ENTRIES 256
#define VECTOR_SYSCALL 0x80

void idt_init(void);

#endif
