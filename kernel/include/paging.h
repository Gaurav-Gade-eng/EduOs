#ifndef EDUOS_PAGING_H
#define EDUOS_PAGING_H
#include "common.h"
#include "interrupts.h"

#define PAGE_PRESENT 0x1u
#define PAGE_RW      0x2u
#define PAGE_USER    0x4u
#define PAGING_NOT_MAPPED 0xFFFFFFFFu

void     paging_init(void);
int      paging_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags);
int      paging_unmap_page(uint32_t virtual_address);
uint32_t paging_get_physical(uint32_t virtual_address);   /* PAGING_NOT_MAPPED if unmapped */
uint32_t paging_mapping_count(void);
uint32_t paging_fault_count(void);
void     paging_print_info(void);

/* Deliberate-fault test: reads an address; returns true if it faulted (and recovered). */
bool     paging_probe(uint32_t virtual_address, uint32_t *value_out);

/* page_fault.c: returns true if the fault was an expected probe and was recovered. */
bool     page_fault_handler(interrupt_frame_t *frame);

/* cpu.asm */
uint32_t paging_probe_read(uint32_t address);
extern char paging_probe_site[];
extern char paging_probe_resume[];

#endif
