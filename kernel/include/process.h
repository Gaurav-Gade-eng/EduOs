#ifndef EDUOS_PROCESS_H
#define EDUOS_PROCESS_H
#include "common.h"
#include "config.h"

typedef enum {
    PROC_UNUSED = 0,
    PROC_NEW,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_TERMINATED
} proc_state_t;

typedef struct process {
    int          pid;
    char         name[PROCESS_NAME_MAX];
    proc_state_t state;
    int          priority;       /* informational; Round Robin ignores it */
    uint32_t    *sp;             /* saved kernel stack pointer */
    uint8_t     *stack_base;
    void       (*entry)(void);
    uint32_t     cpu_ticks;
    int          exit_code;
} process_t;

void       process_init(void);
int        process_create(const char *name, void (*entry)(void), int priority);
int        process_terminate(int pid);
int        process_block(int pid);
int        process_unblock(int pid);
process_t *process_find(int pid);
process_t *process_current(void);
void       process_set_current(process_t *p);   /* scheduler use only */
process_t *process_at(int index);
int        process_count(void);
void       process_exit(int code) __attribute__((noreturn));
void       process_list(void);
void       process_emit_snapshots(void);
const char *process_state_name(proc_state_t s);

/* PIDs reserved for the kernel */
#define PID_IDLE   0
#define PID_KERNEL 1

#endif
