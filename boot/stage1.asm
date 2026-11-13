; EduOS Stage 1 -- 512-byte BIOS boot sector (LBA 0).
; Loads Stage 2 (LBA STAGE2_LBA, STAGE2_SECTORS sectors) to STAGE2_ADDR via INT 13h EDD,
; checks its signature and jumps to it with the BIOS boot drive in DL.
bits 16
org 0x7C00

%include "boot_constants.inc"

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

    ; --- EDD (INT 13h extensions) present? ---
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc  .no_edd
    cmp bx, 0xAA55
    jne .no_edd
    test cx, 1
    jz  .no_edd

    ; --- read Stage 2, 3 attempts ---
    mov cx, 3
.retry:
    push cx
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop cx
    jnc .loaded
    xor ax, ax                  ; reset disk system
    mov dl, [boot_drive]
    int 0x13
    loop .retry
    mov si, msg_read
    jmp fail

.loaded:
    cmp dword [STAGE2_ADDR], 0x21325453   ; 'S','T','2','!'
    jne .bad_meta
    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_ADDR + 4

.no_edd:
    mov si, msg_edd
    jmp fail
.bad_meta:
    mov si, msg_meta
    jmp fail

fail:
    call print
.halt:
    cli
    hlt
    jmp .halt

print:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

boot_drive: db 0
msg_edd:  db "S1 E1: no EDD", 0
msg_read: db "S1 E2: stage2 read", 0
msg_meta: db "S1 E4: bad stage2", 0

align 4
dap:
    db 0x10, 0
    dw STAGE2_SECTORS
    dw STAGE2_ADDR
    dw 0
    dd STAGE2_LBA
    dd 0

times 510 - ($ - $$) db 0
dw 0xAA55
