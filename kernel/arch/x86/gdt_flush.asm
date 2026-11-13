bits 32
section .text
global gdt_flush

; void gdt_flush(void *gdtr)
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload
.reload:
    ret
SECTION .note.GNU-stack noalloc noexec nowrite progbits