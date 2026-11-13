/* int 0x80 dispatcher. EAX = number, EBX/ECX/EDX = args, result in EAX. */
#include "syscall.h"
#include "process.h"
#include "scheduler.h"
#include "timer.h"
#include "console.h"
#include "telemetry.h"
#include "config.h"

static uint32_t total_calls;

void syscall_init(void) { total_calls = 0; }
uint32_t syscall_total(void) { return total_calls; }

const char *syscall_name(uint32_t n) {
    switch (n) {
    case SYS_WRITE: return "write";
    case SYS_GETPID: return "getpid";
    case SYS_GETTICKS: return "getticks";
    case SYS_YIELD: return "yield";
    case SYS_BLOCK: return "block";
    case SYS_UNBLOCK: return "unblock";
    case SYS_EXIT: return "exit";
    case SYS_KILL: return "kill";
    default: return "unknown";
    }
}

void syscall_dispatch(interrupt_frame_t *f) {
    uint32_t n = f->eax, a = f->ebx, b = f->ecx;
    int32_t result = E_INVAL;
    uint32_t pid = (uint32_t)process_current()->pid;
    total_calls++;

    switch (n) {
    case SYS_WRITE:
        /* validate: non-NULL, bounded length, entirely inside the identity-mapped RAM */
        if (a >= PAGE_SIZE && b > 0 && b <= 256 && a + b <= IDENTITY_MAP_LIMIT && a + b > a) {
            char tmp[257];
            memcpy(tmp, (const void *)(uintptr_t)a, b);
            tmp[b] = '\0';
            console_write(tmp);
            result = (int32_t)b;
        }
        break;
    case SYS_GETPID: result = (int32_t)pid; break;
    case SYS_GETTICKS: result = (int32_t)timer_get_ticks(); break;
    case SYS_YIELD:
        result = 0;
        telemetry_emit_syscall(n, syscall_name(n), pid, result);
        f->eax = (uint32_t)result;
        schedule(SW_YIELD);
        return;
    case SYS_BLOCK:
        result = process_block((int)a);       /* may switch away if blocking itself */
        break;
    case SYS_UNBLOCK: result = process_unblock((int)a); break;
    case SYS_KILL: result = process_terminate((int)a); break;
    case SYS_EXIT:
        telemetry_emit_syscall(n, syscall_name(n), pid, 0);
        process_exit((int)a);
    default: break;
    }
    f->eax = (uint32_t)result;
    telemetry_emit_syscall(n, syscall_name(n), pid, result);
}
