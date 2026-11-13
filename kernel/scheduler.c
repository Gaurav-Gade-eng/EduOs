/* Round-Robin scheduler. The idle task (PID 0) runs only when nothing else is READY. */
#include "scheduler.h"
#include "telemetry.h"
#include "console.h"
#include "config.h"
#include "io.h"

typedef struct { uint32_t from, to, total; switch_reason_t reason; } sw_event_t;

#define EVQ 32
static sw_event_t evq[EVQ];
static uint32_t ev_head, ev_count;

static uint32_t quantum_left, switches, total_ticks, idle_ticks;

const char *switch_reason_name(switch_reason_t r) {
    switch (r) {
    case SW_TIMER: return "TIMER";
    case SW_YIELD: return "YIELD";
    case SW_BLOCK: return "BLOCK";
    case SW_EXIT: return "EXIT";
    default: return "KILL";
    }
}

const char *scheduler_algorithm(void) { return "round_robin"; }
uint32_t scheduler_switches(void) { return switches; }
uint32_t scheduler_total_ticks(void) { return total_ticks; }
uint32_t scheduler_idle_ticks(void) { return idle_ticks; }

void scheduler_init(void) { quantum_left = SCHED_QUANTUM_TICKS; }

static process_t *pick_next(process_t *prev) {
    int start = (int)(prev - process_at(0));
    for (int i = 1; i <= MAX_PROCESSES; i++) {
        process_t *p = process_at((start + i) % MAX_PROCESSES);
        if (p->state == PROC_READY && p->pid != PID_IDLE) return p;
    }
    return process_find(PID_IDLE);
}

void schedule(switch_reason_t reason) {
    uint32_t flags = irq_save();
    process_t *prev = process_current();
    if (prev->state == PROC_RUNNING) prev->state = PROC_READY;
    process_t *next = pick_next(prev);
    quantum_left = SCHED_QUANTUM_TICKS;
    if (next == prev) {
        prev->state = PROC_RUNNING;
        irq_restore(flags);
        return;
    }
    next->state = PROC_RUNNING;
    switches++;
    if (ev_count < EVQ) {
        sw_event_t *e = &evq[(ev_head + ev_count) % EVQ];
        e->from = (uint32_t)prev->pid; e->to = (uint32_t)next->pid; e->reason = reason; e->total = switches;
        ev_count++;
    }
    process_set_current(next);
    context_switch(&prev->sp, next->sp);
    irq_restore(flags);        /* runs when `prev` is scheduled back in */
}

/* ISR context */
void scheduler_tick(void) {
    process_t *c = process_current();
    total_ticks++;
    c->cpu_ticks++;
    if (c->pid == PID_IDLE) idle_ticks++;
    if (quantum_left > 0 && --quantum_left == 0) schedule(SW_TIMER);
}

void scheduler_drain_events(void) {
    for (;;) {
        sw_event_t e;
        uint32_t f = irq_save();
        if (ev_count == 0) { irq_restore(f); return; }
        e = evq[ev_head];
        ev_head = (ev_head + 1) % EVQ;
        ev_count--;
        irq_restore(f);
        telemetry_emit_scheduler_switch(e.from, e.to, switch_reason_name(e.reason), e.total);
    }
}

void scheduler_emit_snapshot(void) {
    telemetry_emit_scheduler_snapshot(scheduler_algorithm(), SCHED_QUANTUM_TICKS,
                                      (uint32_t)process_current()->pid, switches, total_ticks, idle_ticks);
}

void scheduler_print(void) {
    console_printf("algorithm=%s quantum=%u ticks current_pid=%d switches=%u total_ticks=%u idle_ticks=%u\n",
                   scheduler_algorithm(), SCHED_QUANTUM_TICKS, process_current()->pid, switches,
                   total_ticks, idle_ticks);
    scheduler_emit_snapshot();
}
