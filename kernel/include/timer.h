#ifndef EDUOS_TIMER_H
#define EDUOS_TIMER_H
#include "common.h"

void timer_init(unsigned int frequency_hz);
uint32_t timer_get_ticks(void);
uint32_t timer_get_hz(void);
void timer_interrupt_handler(void);

#endif
