#ifndef EDUOS_BOOT_INFO_H
#define EDUOS_BOOT_INFO_H

#include "common.h"
#include "layout.h"

#define EDUOS_BOOT_MAGIC       BOOT_MAGIC
#define BOOT_MAX_MEMORY_RANGES BOOT_MAX_RANGES
#define BOOT_FLAG_MEMMAP_VALID 0x1u

/* Must match boot/stage2.asm exactly (checked by _Static_assert below). */
typedef struct {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;           /* E820 type: 1 = usable */
} boot_memory_range_t;

typedef struct {
    uint32_t magic;          /* offset 0  */
    uint32_t version;        /* offset 4  */
    uint32_t boot_drive;     /* offset 8  */
    uint32_t kernel_start;   /* offset 12 */
    uint32_t kernel_end;     /* offset 16 (end of loaded image, excl. bss) */
    uint32_t memory_range_count; /* offset 20 */
    uint32_t flags;          /* offset 24: bit0 = memory map valid */
    boot_memory_range_t memory_ranges[BOOT_MAX_MEMORY_RANGES]; /* offset 28 */
} boot_info_t;

_Static_assert(sizeof(boot_memory_range_t) == 20, "E820 entry size");
_Static_assert(offsetof(boot_info_t, memory_ranges) == 28, "boot_info range offset");

#endif
