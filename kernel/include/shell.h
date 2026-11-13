#ifndef EDUOS_SHELL_H
#define EDUOS_SHELL_H
#include "common.h"

void shell_init(void);
void shell_poll(void);   /* consume pending keystrokes; run a command on Enter */

#endif
