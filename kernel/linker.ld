/*
 * EduOS Kernel Linker Script
 * Architecture: 32-bit x86
 * Load address: 0x10000
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
    stack  PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    . = KERNEL_LOAD_ADDRESS;
    __kernel_start = .;

    .text : ALIGN(16)
    {
        *(.text.boot)
        *(.text .text.*)
    } :text

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    } :rodata

    .data : ALIGN(16)
    {
        *(.data .data.*)
    } :data

    .bss : ALIGN(4096)
    {
        __bss_start = .;

        *(COMMON)
        *(.bss .bss.*)

        . = ALIGN(4);
        __bss_end = .;
    } :data

    . = ALIGN(4096);
    __kernel_end = .;

    ASSERT(
        __kernel_end <= KERNEL_MAX_END,
        "kernel image (incl. .bss) exceeds KERNEL_MAX_END"
    )

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}