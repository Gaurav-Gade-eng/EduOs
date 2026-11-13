#ifndef EDUOS_CONFIG_H
#define EDUOS_CONFIG_H

#include "layout.h"   /* generated from config/layout.conf */

#define EDUOS_VERSION        "0.1.0"
#define EDUOS_ARCH           "x86-32"
#define TELEMETRY_VERSION    1

#define PAGE_SIZE            4096u
#define IDENTITY_MAP_LIMIT   0x01000000u   /* first 16 MiB identity mapped */
#define FRAME_POOL_BASE      0x00100000u   /* frame allocator manages 1 MiB .. 16 MiB */
#define FRAME_POOL_END       IDENTITY_MAP_LIMIT
#define DYNAMIC_MAP_BASE     0x40000000u   /* educational dynamic mapping window */
#define DYNAMIC_MAP_END      0x80000000u

#define TIMER_HZ             100u
#define SCHED_QUANTUM_TICKS  10u
#define SNAPSHOT_INTERVAL_TICKS 500u       /* periodic telemetry snapshot (5 s) */

#define MAX_PROCESSES        16
#define PROCESS_NAME_MAX     16
#define TASK_STACK_SIZE      8192u
#define KERNEL_STACK_SIZE    16384u

#endif
