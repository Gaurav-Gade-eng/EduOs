; Kernel entry. Stage 2 jumps here (KERNEL_LOAD_ADDR) in 32-bit protected mode with
; EBX = &boot_info, EAX = BOOT_MAGIC, interrupts disabled. Must be the first byte of the image.
bits 32

section .text.boot
global _start
extern kernel_main
extern __bss_start
extern __bss_end

_start:
    cli
    cld
    ; zero .bss (the loader only copies the file image)
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    shr ecx, 2
    xor eax, eax
    rep stosd
    mov esp, kernel_stack_top
    xor ebp, ebp
    push ebx                    ; arg: boot_info_t *
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
global kernel_stack_bottom
global kernel_stack_top
kernel_stack_bottom:
    resb 16384
kernel_stack_top:
SECTION .note.GNU-stack noalloc noexec nowrite progbits