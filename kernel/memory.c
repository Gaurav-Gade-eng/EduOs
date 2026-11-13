/* Physical frame allocator: bitmap over 1 MiB..16 MiB (3840 frames of 4 KiB). */
#include "memory.h"
#include "telemetry.h"
#include "console.h"
#include "config.h"
#include "io.h"

#define TOTAL_FRAMES ((FRAME_POOL_END - FRAME_POOL_BASE) / PAGE_SIZE)

static uint32_t bitmap[(TOTAL_FRAMES + 31) / 32];
static uint32_t used, allocs, frees, hint;
static bool map_valid;

static bool test_bit(uint32_t i) { return bitmap[i / 32] & (1u << (i % 32)); }
static void set_bit(uint32_t i) { bitmap[i / 32] |= 1u << (i % 32); }
static void clear_bit(uint32_t i) { bitmap[i / 32] &= ~(1u << (i % 32)); }

/* is [addr, addr+PAGE_SIZE) inside a usable E820 range? */
static bool usable(const boot_info_t *bi, uint32_t addr) {
    for (uint32_t i = 0; i < bi->memory_range_count && i < BOOT_MAX_MEMORY_RANGES; i++) {
        const boot_memory_range_t *r = &bi->memory_ranges[i];
        if (r->type != 1 || r->base_high != 0) continue;
        uint32_t base = r->base_low;
        uint32_t len = r->length_high ? 0xFFFFFFFFu : r->length_low;
        uint32_t end = (base + len < base) ? 0xFFFFFFFFu : base + len;   /* clamp overflow */
        if (addr >= base && addr + PAGE_SIZE <= end) return true;
    }
    return false;
}

void memory_init(const boot_info_t *bi) {
    memset(bitmap, 0, sizeof bitmap);
    used = allocs = frees = hint = 0;
    map_valid = bi && (bi->flags & BOOT_FLAG_MEMMAP_VALID);
    if (map_valid) {
        for (uint32_t i = 0; i < TOTAL_FRAMES; i++)
            if (!usable(bi, FRAME_POOL_BASE + i * PAGE_SIZE)) { set_bit(i); used++; }
    }
}

void *frame_alloc(void) {
    uint32_t f = irq_save();
    for (uint32_t n = 0; n < TOTAL_FRAMES; n++) {
        uint32_t i = (hint + n) % TOTAL_FRAMES;
        if (!test_bit(i)) {
            set_bit(i); used++; allocs++; hint = i + 1;
            irq_restore(f);
            return (void *)(uintptr_t)(FRAME_POOL_BASE + i * PAGE_SIZE);
        }
    }
    irq_restore(f);
    return NULL;
}

void *frame_alloc_zeroed(void) {
    void *p = frame_alloc();
    if (p) memset(p, 0, PAGE_SIZE);
    return p;
}

int frame_free(void *physical_address) {
    uint32_t a = (uint32_t)(uintptr_t)physical_address;
    if (a < FRAME_POOL_BASE || a >= FRAME_POOL_END || (a & (PAGE_SIZE - 1))) return E_INVAL;
    uint32_t i = (a - FRAME_POOL_BASE) / PAGE_SIZE;
    uint32_t f = irq_save();
    if (!test_bit(i)) { irq_restore(f); return E_INVAL; }      /* double free */
    clear_bit(i); used--; frees++;
    if (i < hint) hint = i;
    irq_restore(f);
    return E_OK;
}

uint32_t memory_total_frames(void) { return TOTAL_FRAMES; }
uint32_t memory_used_frames(void) { return used; }
uint32_t memory_free_frames(void) { return TOTAL_FRAMES - used; }
uint32_t memory_alloc_count(void) { return allocs; }
uint32_t memory_free_count(void) { return frees; }
bool memory_map_was_valid(void) { return map_valid; }

void memory_print(void) {
    console_printf("frames: total=%u used=%u free=%u (frame=%u B, pool %u KiB used)\n",
                   memory_total_frames(), used, memory_free_frames(), PAGE_SIZE, used * 4);
    console_printf("allocs=%u frees=%u e820=%s\n", allocs, frees, map_valid ? "valid" : "unavailable");
    memory_emit_snapshot();
}

void memory_emit_snapshot(void) {
    telemetry_emit_memory_snapshot(memory_total_frames(), used, memory_free_frames(), PAGE_SIZE, allocs, frees);
}
