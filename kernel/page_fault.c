#include "paging.h"
#include "telemetry.h"
#include "console.h"
#include "panic.h"

extern void paging_note_fault(void);

/* Returns true when the fault was a deliberate probe and execution may resume. */
bool page_fault_handler(interrupt_frame_t *f) {
    uint32_t addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(addr));
    paging_note_fault();
    bool expected = (f->eip == (uint32_t)(uintptr_t)paging_probe_site);
    telemetry_emit_paging_fault(addr, f->err_code, expected, f->eip);
    if (expected) {
        f->eip = (uint32_t)(uintptr_t)paging_probe_resume;
        f->eax = 0xFA17FA17u;           /* marker read back by paging_probe() */
        return true;
    }
    console_printf("\nPAGE FAULT addr=0x%08x err=0x%x (%s,%s) eip=0x%08x\n", addr, f->err_code,
                   (f->err_code & 1) ? "protection" : "not-present", (f->err_code & 2) ? "write" : "read",
                   f->eip);
    kernel_panic("PAGE_FAULT", "unexpected page fault");
}
