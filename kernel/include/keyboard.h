#ifndef EDUOS_KEYBOARD_H
#define EDUOS_KEYBOARD_H
#include "common.h"

void keyboard_init(void);
int  keyboard_getchar(void);   /* -1 when no key is waiting */

#endif
