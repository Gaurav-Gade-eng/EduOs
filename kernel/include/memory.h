#ifndef EDUOS_MEMORY_H
#define EDUOS_MEMORY_H
#include "common.h"
#include "boot_info.h"

void     memory_init(const boot_info_t *boot_info);
void    *frame_alloc(void);          /* physical address of a free 4 KiB frame, or NULL */
void    *frame_alloc_zeroed(void);
int      frame_free(void *physical_address);   /* E_OK or E_INVAL (bad/double free) */
uint32_t memory_total_frames(void);
uint32_t memory_used_frames(void);
uint32_t memory_free_frames(void);
uint32_t memory_alloc_count(void);
uint32_t memory_free_count(void);
bool     memory_map_was_valid(void);
void     memory_print(void);
void     memory_emit_snapshot(void);

#endif
