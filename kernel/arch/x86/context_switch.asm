; void context_switch(uint32_t **old_sp, uint32_t *new_sp)
; Saves the callee-saved registers (cdecl) on the current stack, stores ESP in *old_sp,
; loads new_sp and restores the other task's registers. For a brand-new task the stack is
; pre-built (see process.c) so the final `ret` enters the task trampoline.
bits 32
section .text
global context_switch

context_switch:
    mov eax, [esp + 4]          ; old_sp
    mov edx, [esp + 8]          ; new_sp
    push ebp
    push ebx
    push esi
    push edi
    mov [eax], esp
    mov esp, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
SECTION .note.GNU-stack noalloc noexec nowrite progbits