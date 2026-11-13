#ifndef EDUOS_TELEMETRY_H
#define EDUOS_TELEMETRY_H
#include "common.h"

/*
 * Line protocol (version 1):  EDUOS|1|<seq:8>|CATEGORY|EVENT|key=value|key=value
 * Values are percent-encoded (everything except A-Za-z0-9 . _ - : /) so a value
 * can never contain '|', '=', or a newline and forge another record.
 * emit_* functions only ENQUEUE (safe in interrupt context); telemetry_flush()
 * transmits queued lines over COM1 from the kernel main loop.
 */

void telemetry_init(void);
void telemetry_flush(void);

void telemetry_emit_boot(const char *version, const char *architecture);
void telemetry_emit_kernel_ready(void);
void telemetry_emit_module_ready(const char *name, const char *detail);
void telemetry_emit_module_error(const char *name, const char *reason);
void telemetry_emit_info(const char *message_id, const char *detail);
void telemetry_emit_panic(const char *code, const char *message);

void telemetry_emit_process_create(uint32_t pid, const char *name, const char *state, int priority);
void telemetry_emit_process_state(uint32_t pid, const char *state, const char *reason);
void telemetry_emit_process_exit(uint32_t pid, int exit_code, const char *reason);
void telemetry_emit_process_snapshot(uint32_t pid, const char *name, const char *state,
                                     int priority, uint32_t cpu_ticks);

void telemetry_emit_scheduler_snapshot(const char *algorithm, uint32_t quantum, uint32_t current_pid,
                                       uint32_t switches, uint32_t total_ticks, uint32_t idle_ticks);
void telemetry_emit_scheduler_switch(uint32_t from_pid, uint32_t to_pid, const char *reason, uint32_t total);

void telemetry_emit_memory_snapshot(uint32_t total, uint32_t used, uint32_t free_frames, uint32_t frame_size,
                                    uint32_t allocs, uint32_t frees);

void telemetry_emit_paging_map(uint32_t virt, uint32_t phys, const char *flags, const char *result);
void telemetry_emit_paging_unmap(uint32_t virt, const char *result);
void telemetry_emit_paging_fault(uint32_t address, uint32_t error, bool expected, uint32_t eip);

void telemetry_emit_syscall(uint32_t number, const char *name, uint32_t pid, int result);
void telemetry_emit_interrupt_count(uint32_t irq, uint32_t count);

void telemetry_emit_disk_ready(const char *device, uint32_t sectors, uint32_t sector_size);
void telemetry_emit_disk_op(const char *op, uint32_t lba, uint32_t count, const char *result);

void telemetry_emit_fs_mount(uint32_t version, uint32_t block_size, uint32_t total_blocks,
                             uint32_t files, const char *result);
void telemetry_emit_fs_format(uint32_t total_blocks, const char *result);
void telemetry_emit_fs_op(const char *event, const char *name, const char *size_key, uint32_t size,
                          uint32_t files, const char *result);

void telemetry_emit_stats(void);     /* created / sent / dropped counters */
uint32_t telemetry_created(void);
uint32_t telemetry_sent(void);
uint32_t telemetry_dropped(void);

#endif
