; EduOS Stage 2 loader (loaded at STAGE2_ADDR by Stage 1).
;  1. validate build metadata   2. load kernel from disk in bounded batches (EDD)
;  3. E820 memory map -> boot_info   4. enable A20   5. GDT + protected mode
;  6. jump to kernel entry with EBX = &boot_info, EAX = BOOT_MAGIC
bits 16
%include "boot_constants.inc"
%include "loader_metadata.inc"          ; KERNEL_BYTES, KERNEL_SECTORS (generated)
org STAGE2_ADDR

%if KERNEL_SECTORS > KERNEL_MAX_SECTORS
%error "kernel exceeds KERNEL_MAX_SECTORS"
%endif

dd 0x21325453                           ; 'ST2!' signature checked by Stage 1

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, REAL_STACK_TOP
    sti
    cld
    mov [boot_drive], dl

    mov si, msg_ok
    call print

    ; ---- runtime sanity check of build metadata ----
    mov ax, [kernel_sectors]
    test ax, ax
    jz  err_size
    cmp ax, KERNEL_MAX_SECTORS
    ja  err_size

    ; ---- load kernel ----
    mov ax, [kernel_sectors]
    mov [remaining], ax
    mov word [cur_seg], KERNEL_LOAD_ADDR >> 4
.batch:
    mov ax, [remaining]
    test ax, ax
    jz  .loaded
    cmp ax, KERNEL_BATCH_SECTORS
    jbe .count_ok
    mov ax, KERNEL_BATCH_SECTORS
.count_ok:
    mov [dap_count], ax
    mov bx, [cur_seg]
    mov [dap_seg], bx
    mov word [dap_off], 0
    mov cx, 3
.retry:
    push cx
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop cx
    jnc .read_ok
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    loop .retry
    jmp err_kread
.read_ok:
    mov ax, [dap_count]
    add [dap_lba], ax
    adc word [dap_lba + 2], 0
    sub [remaining], ax
    shl ax, 5                           ; sectors * 512 / 16 = paragraphs
    add [cur_seg], ax
    jmp .batch
.loaded:

    ; ---- boot_info ----
    mov di, BOOT_INFO_ADDR
    mov cx, (BOOT_INFO_RANGES_OFFSET + BOOT_MAX_RANGES * 20) / 4
    xor eax, eax
    rep stosd
    mov dword [BOOT_INFO_ADDR + 0], BOOT_MAGIC
    mov dword [BOOT_INFO_ADDR + 4], BOOT_VERSION
    movzx eax, byte [boot_drive]
    mov [BOOT_INFO_ADDR + 8], eax
    mov dword [BOOT_INFO_ADDR + 12], KERNEL_LOAD_ADDR
    mov dword [BOOT_INFO_ADDR + 16], KERNEL_LOAD_ADDR + KERNEL_BYTES

    ; ---- E820 memory map ----
    xor ebx, ebx
    xor bp, bp
    mov di, BOOT_INFO_ADDR + BOOT_INFO_RANGES_OFFSET
.e820:
    mov eax, 0xE820
    mov edx, 0x534D4150                 ; 'SMAP'
    mov ecx, 20
    int 0x15
    jc  .e820_end                       ; carry on first call => unsupported
    cmp eax, 0x534D4150
    jne .e820_end
    cmp bp, BOOT_MAX_RANGES
    jae err_mmap
    add di, 20
    inc bp
    test ebx, ebx
    jnz .e820
.e820_end:
    movzx eax, bp
    mov [BOOT_INFO_ADDR + 20], eax
    test bp, bp
    jz  .no_map
    mov dword [BOOT_INFO_ADDR + 24], 1  ; flags bit0 = memory map valid
.no_map:

    ; ---- A20 ----
    call a20_check
    jnz .a20_ok
    mov ax, 0x2401                      ; BIOS method
    int 0x15
    call a20_check
    jnz .a20_ok
    in  al, 0x92                        ; fast A20
    or  al, 2
    and al, 0xFE
    out 0x92, al
    mov cx, 0xFFFF
.a20_wait:
    call a20_check
    jnz .a20_ok
    loop .a20_wait
    jmp err_a20
.a20_ok:

    ; ---- protected mode ----
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

; ZF = 1 -> A20 DISABLED (memory wraps), ZF = 0 -> enabled
a20_check:
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov ax, 0xFFFF
    mov es, ax
    mov si, 0x0500
    mov di, 0x0510
    mov al, [ds:si]
    push ax
    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF
    cmp byte [ds:si], 0xFF
    pop ax
    mov [ds:si], al
    pop es
    pop ds
    ret

err_size:
    mov si, msg_size
    jmp fail
err_kread:
    mov si, msg_kread
    jmp fail
err_mmap:
    mov si, msg_mmap
    jmp fail
err_a20:
    mov si, msg_a20
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

; ------------------------------------------------------------------ 32-bit
bits 32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, PM_STACK_TOP
    mov ebx, BOOT_INFO_ADDR
    mov eax, BOOT_MAGIC
    mov ecx, KERNEL_LOAD_ADDR
    jmp ecx                             ; kernel _start is the first byte of the image
bits 16

; ------------------------------------------------------------------ data
boot_drive:     db 0
remaining:      dw 0
cur_seg:        dw 0
kernel_sectors: dw KERNEL_SECTORS

msg_ok:    db "STAGE2_OK", 13, 10, 0
msg_size:  db "S2 E4: invalid kernel size", 0
msg_kread: db "S2 E3: kernel read", 0
msg_mmap:  db "S2 E5: memmap overflow", 0
msg_a20:   db "S2 E6: A20 failed", 0

align 4
dap:
    db 0x10, 0
dap_count: dw 0
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dd KERNEL_LBA
           dd 0

align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF               ; 0x08 code, base 0, limit 4G, 32-bit
    dq 0x00CF92000000FFFF               ; 0x10 data
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

times (STAGE2_SECTORS * 512) - ($ - $$) db 0   ; fails the build if Stage 2 outgrows its area
