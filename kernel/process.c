/*
 * Process table. Tasks are kernel-mode (ring 0). PIDs are never reused.
 * Valid transitions: NEW->READY, READY<->RUNNING, READY/RUNNING->BLOCKED, BLOCKED->READY,
 * any->TERMINATED. PID 0 (idle) and PID 1 (kernel/shell) cannot be blocked or killed.
 */
#include "process.h"
#include "scheduler.h"
#include "telemetry.h"
#include "console.h"
#include "io.h"

static process_t table[MAX_PROCESSES];
static uint8_t task_stacks[MAX_PROCESSES][TASK_STACK_SIZE] __attribute__((aligned(16)));
static process_t *current;
static int next_pid;

const char *process_state_name(proc_state_t s) {
    switch (s) {
    case PROC_NEW: return "NEW";
    case PROC_READY: return "READY";
    case PROC_RUNNING: return "RUNNING";
    case PROC_BLOCKED: return "BLOCKED";
    case PROC_TERMINATED: return "TERMINATED";
    default: return "UNUSED";
    }
}

process_t *process_current(void) { return current; }
void process_set_current(process_t *p) { current = p; }
process_t *process_at(int index) { return (index >= 0 && index < MAX_PROCESSES) ? &table[index] : NULL; }

process_t *process_find(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (table[i].state != PROC_UNUSED && table[i].pid == pid) return &table[i];
    return NULL;
}

int process_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (table[i].state != PROC_UNUSED && table[i].state != PROC_TERMINATED) n++;
    return n;
}

static void task_trampoline(void) {
    cpu_sti();                       /* a new task starts with interrupts enabled */
    current->entry();
    process_exit(0);
}

static void idle_task(void) { for (;;) cpu_hlt(); }

static process_t *alloc_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (table[i].state == PROC_UNUSED) return &table[i];
    for (int i = 0; i < MAX_PROCESSES; i++)          /* reclaim a finished task */
        if (table[i].state == PROC_TERMINATED && &table[i] != current) {
            table[i].state = PROC_UNUSED;
            return &table[i];
        }
    return NULL;
}

int process_create(const char *name, void (*entry)(void), int priority) {
    if (!name || !entry) return E_INVAL;
    uint32_t f = irq_save();
    process_t *p = alloc_slot();
    if (!p) { irq_restore(f); return E_NOMEM; }
    int idx = (int)(p - table);
    memset(p, 0, sizeof *p);
    p->pid = next_pid++;
    strlcpy(p->name, name, sizeof p->name);
    p->priority = priority;
    p->entry = entry;
    p->stack_base = task_stacks[idx];
    p->state = PROC_NEW;

    /* Pre-built frame popped by context_switch: edi,esi,ebx,ebp, then `ret` -> trampoline */
    uint32_t *top = (uint32_t *)(void *)(p->stack_base + TASK_STACK_SIZE);
    *--top = 0;                                   /* fake return address for the trampoline */
    *--top = (uint32_t)(uintptr_t)task_trampoline;
    *--top = 0; *--top = 0; *--top = 0; *--top = 0;
    p->sp = top;
    p->state = PROC_READY;
    int pid = p->pid;
    irq_restore(f);
    telemetry_emit_process_create((uint32_t)pid, name, "READY", priority);
    return pid;
}

void process_init(void) {
    memset(table, 0, sizeof table);
    next_pid = 0;
    int idle = process_create("idle", idle_task, 0);          /* PID 0 */
    UNUSED(idle);
    /* PID 1 represents the already-running boot context (kernel main loop / shell). */
    process_t *k = &table[1];
    memset(k, 0, sizeof *k);
    k->pid = next_pid++;
    strlcpy(k->name, "kernel", sizeof k->name);
    k->priority = 1;
    k->state = PROC_RUNNING;
    current = k;
    telemetry_emit_process_create((uint32_t)k->pid, k->name, "RUNNING", k->priority);
}

int process_block(int pid) {
    uint32_t f = irq_save();
    process_t *p = process_find(pid);
    int rc = E_OK;
    if (!p) rc = E_NOENT;
    else if (pid == PID_IDLE || pid == PID_KERNEL) rc = E_PERM;
    else if (p->state != PROC_READY && p->state != PROC_RUNNING) rc = E_STATE;
    else p->state = PROC_BLOCKED;
    irq_restore(f);
    if (rc == E_OK) {
        telemetry_emit_process_state((uint32_t)pid, "BLOCKED", "BLOCK");
        if (p == current) schedule(SW_BLOCK);
    }
    return rc;
}

int process_unblock(int pid) {
    uint32_t f = irq_save();
    process_t *p = process_find(pid);
    int rc = E_OK;
    if (!p) rc = E_NOENT;
    else if (p->state != PROC_BLOCKED) rc = E_STATE;
    else p->state = PROC_READY;
    irq_restore(f);
    if (rc == E_OK) telemetry_emit_process_state((uint32_t)pid, "READY", "WAKE");
    return rc;
}

int process_terminate(int pid) {
    uint32_t f = irq_save();
    process_t *p = process_find(pid);
    int rc = E_OK;
    if (!p) rc = E_NOENT;
    else if (pid == PID_IDLE || pid == PID_KERNEL) rc = E_PERM;
    else if (p->state == PROC_TERMINATED) rc = E_STATE;
    irq_restore(f);
    if (rc != E_OK) return rc;
    if (p == current) process_exit(-1);
    f = irq_save();
    p->state = PROC_TERMINATED;
    p->exit_code = -1;
    irq_restore(f);
    telemetry_emit_process_exit((uint32_t)pid, -1, "KILL");
    return E_OK;
}

void process_exit(int code) {
    uint32_t f = irq_save();
    (void)f;                                   /* never restored: this task is finished */
    current->state = PROC_TERMINATED;
    current->exit_code = code;
    telemetry_emit_process_exit((uint32_t)current->pid, code, "EXIT");
    schedule(SW_EXIT);
    for (;;) cpu_hlt();                        /* unreachable */
}

void process_list(void) {
    console_write("PID  NAME             STATE       PRIO  TICKS\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &table[i];
        if (p->state == PROC_UNUSED) continue;
        console_printf("%3d  %-16s %-11s %4d  %u\n", p->pid, p->name, process_state_name(p->state),
                       p->priority, p->cpu_ticks);
    }
}

void process_emit_snapshots(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &table[i];
        if (p->state == PROC_UNUSED) continue;
        telemetry_emit_process_snapshot((uint32_t)p->pid, p->name, process_state_name(p->state),
                                        p->priority, p->cpu_ticks);
    }
}
