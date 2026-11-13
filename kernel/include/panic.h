#ifndef EDUOS_PANIC_H
#define EDUOS_PANIC_H
#include "common.h"

/* Never returns. code: short identifier (A-Z0-9_), msg: human text. */
void kernel_panic(const char *code, const char *msg) __attribute__((noreturn));

#endif
