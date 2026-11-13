#ifndef EDUOS_SYSCALL_H
#define EDUOS_SYSCALL_H
#include "common.h"
#include "interrupts.h"

/* ABI: int 0x80; EAX = number, EBX/ECX/EDX = args 1..3; result returned in EAX. */
#define SYS_WRITE    1   /* (ptr, len)  -> bytes written or <0   */
#define SYS_GETPID   2
#define SYS_GETTICKS 3
#define SYS_YIELD    4
#define SYS_BLOCK    5   /* (pid)       -> 0 or <0               */
#define SYS_UNBLOCK  6   /* (pid)                                */
#define SYS_EXIT     7   /* (code)      does not return          */
#define SYS_KILL     8   /* (pid)                                */

void     syscall_init(void);
void     syscall_dispatch(interrupt_frame_t *frame);
uint32_t syscall_total(void);
const char *syscall_name(uint32_t number);

static inline int32_t syscall3(uint32_t n, uint32_t a, uint32_t b, uint32_t c) {
    int32_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
static inline int32_t sys_write(const char *s, uint32_t len) { return syscall3(SYS_WRITE, (uint32_t)(uintptr_t)s, len, 0); }
static inline int32_t sys_getpid(void) { return syscall3(SYS_GETPID, 0, 0, 0); }
static inline uint32_t sys_getticks(void) { return (uint32_t)syscall3(SYS_GETTICKS, 0, 0, 0); }
static inline void sys_yield(void) { (void)syscall3(SYS_YIELD, 0, 0, 0); }
static inline int32_t sys_block(int pid) { return syscall3(SYS_BLOCK, (uint32_t)pid, 0, 0); }
static inline int32_t sys_unblock(int pid) { return syscall3(SYS_UNBLOCK, (uint32_t)pid, 0, 0); }
static inline int32_t sys_kill(int pid) { return syscall3(SYS_KILL, (uint32_t)pid, 0, 0); }
static inline void sys_exit(int code) { (void)syscall3(SYS_EXIT, (uint32_t)code, 0, 0); for (;;) { } }

#endif
