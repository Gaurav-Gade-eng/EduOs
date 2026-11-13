; CPU helpers.
bits 32
section .text
global paging_probe_read
global paging_probe_site
global paging_probe_resume

; uint32_t paging_probe_read(uint32_t address)
; The faulting load sits at a known label so page_fault_handler() can recognise a
; deliberate, expected fault and resume at paging_probe_resume with EAX = 0xFA17FA17.
paging_probe_read:
    mov edx, [esp + 4]
paging_probe_site:
    mov eax, [edx]
paging_probe_resume:
    ret
SECTION .note.GNU-stack noalloc noexec nowrite progbits