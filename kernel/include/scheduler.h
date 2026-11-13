#ifndef EDUOS_SCHEDULER_H
#define EDUOS_SCHEDULER_H
#include "common.h"
#include "process.h"

typedef enum { SW_TIMER, SW_YIELD, SW_BLOCK, SW_EXIT, SW_KILL } switch_reason_t;

void     scheduler_init(void);
void     schedule(switch_reason_t reason);
void     scheduler_tick(void);                 /* timer ISR hook: account + preempt */
uint32_t scheduler_switches(void);
uint32_t scheduler_total_ticks(void);
uint32_t scheduler_idle_ticks(void);
const char *scheduler_algorithm(void);
void     scheduler_drain_events(void);         /* emit queued switch events (non-ISR) */
void     scheduler_print(void);
void     scheduler_emit_snapshot(void);
const char *switch_reason_name(switch_reason_t r);

/* context_switch.asm */
void context_switch(uint32_t **old_sp, uint32_t *new_sp);

#endif
