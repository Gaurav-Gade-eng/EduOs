/*
 * 32-bit two-level paging. First 16 MiB identity mapped (page 0 left unmapped to catch NULL
 * dereferences). Dynamic educational mappings live in [DYNAMIC_MAP_BASE, DYNAMIC_MAP_END).
 * Page tables for dynamic mappings come from the frame allocator (identity-mapped, so
 * physical == virtual when the kernel touches them).
 */
#include "paging.h"
#include "memory.h"
#include "telemetry.h"
#include "console.h"
#include "config.h"
#include "io.h"

#define NUM_IDENTITY_TABLES (IDENTITY_MAP_LIMIT / (PAGE_SIZE * 1024))

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t identity_tables[NUM_IDENTITY_TABLES][1024] __attribute__((aligned(4096)));
static uint32_t mappings, faults;

static inline void invlpg(uint32_t addr) { __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory"); }

void paging_init(void) {
    for (uint32_t t = 0; t < NUM_IDENTITY_TABLES; t++) {
        for (uint32_t i = 0; i < 1024; i++) {
            uint32_t phys = (t * 1024 + i) * PAGE_SIZE;
            identity_tables[t][i] = (t == 0 && i == 0) ? 0 : (phys | PAGE_PRESENT | PAGE_RW);
        }
        page_directory[t] = (uint32_t)(uintptr_t)identity_tables[t] | PAGE_PRESENT | PAGE_RW;
    }
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)(uintptr_t)page_directory) : "memory");
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static const char *flag_name(uint32_t flags) { return (flags & PAGE_RW) ? "RW" : "RO"; }

int paging_map_page(uint32_t v, uint32_t p, uint32_t flags) {
    int rc = E_OK;
    if ((v & (PAGE_SIZE - 1)) || (p & (PAGE_SIZE - 1))) rc = E_INVAL;
    else if (v < DYNAMIC_MAP_BASE || v >= DYNAMIC_MAP_END) rc = E_PERM;   /* identity region is protected */
    if (rc == E_OK) {
        uint32_t pdi = v >> 22, pti = (v >> 12) & 0x3FF;
        uint32_t f = irq_save();
        if (!(page_directory[pdi] & PAGE_PRESENT)) {
            void *t = frame_alloc_zeroed();
            if (!t) rc = E_NOMEM;
            else page_directory[pdi] = (uint32_t)(uintptr_t)t | PAGE_PRESENT | PAGE_RW;
        }
        if (rc == E_OK) {
            uint32_t *table = (uint32_t *)(uintptr_t)(page_directory[pdi] & ~0xFFFu);
            if (table[pti] & PAGE_PRESENT) rc = E_EXIST;
            else {
                table[pti] = (p & ~0xFFFu) | (flags & (PAGE_RW | PAGE_USER)) | PAGE_PRESENT;
                invlpg(v);
                mappings++;
            }
        }
        irq_restore(f);
    }
    telemetry_emit_paging_map(v, p, flag_name(flags), err_name(rc));
    return rc;
}

int paging_unmap_page(uint32_t v) {
    int rc = E_OK;
    if (v & (PAGE_SIZE - 1)) rc = E_INVAL;
    else if (v < DYNAMIC_MAP_BASE || v >= DYNAMIC_MAP_END) rc = E_PERM;
    else {
        uint32_t pdi = v >> 22, pti = (v >> 12) & 0x3FF;
        uint32_t f = irq_save();
        if (!(page_directory[pdi] & PAGE_PRESENT)) rc = E_NOENT;
        else {
            uint32_t *table = (uint32_t *)(uintptr_t)(page_directory[pdi] & ~0xFFFu);
            if (!(table[pti] & PAGE_PRESENT)) rc = E_NOENT;
            else { table[pti] = 0; invlpg(v); mappings--; }
        }
        irq_restore(f);
    }
    telemetry_emit_paging_unmap(v, err_name(rc));
    return rc;
}

uint32_t paging_get_physical(uint32_t v) {
    uint32_t pde = page_directory[v >> 22];
    if (!(pde & PAGE_PRESENT)) return PAGING_NOT_MAPPED;
    uint32_t pte = ((uint32_t *)(uintptr_t)(pde & ~0xFFFu))[(v >> 12) & 0x3FF];
    if (!(pte & PAGE_PRESENT)) return PAGING_NOT_MAPPED;
    return (pte & ~0xFFFu) | (v & 0xFFFu);
}

uint32_t paging_mapping_count(void) { return mappings; }
uint32_t paging_fault_count(void) { return faults; }
void paging_note_fault(void) { faults++; }

bool paging_probe(uint32_t v, uint32_t *value_out) {
    uint32_t r = paging_probe_read(v);
    if (r == 0xFA17FA17u && paging_get_physical(v) == PAGING_NOT_MAPPED) return true;
    if (value_out) *value_out = r;
    return false;
}

void paging_print_info(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    console_printf("paging: enabled, cr3=0x%08x, identity map 0..%u MiB (page 0 unmapped)\n", cr3,
                   IDENTITY_MAP_LIMIT >> 20);
    console_printf("dynamic window 0x%08x..0x%08x, active mappings=%u, faults=%u\n", DYNAMIC_MAP_BASE,
                   DYNAMIC_MAP_END, mappings, faults);
}
