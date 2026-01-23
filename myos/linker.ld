ENTRY(_start)
SECTIONS
{
    /* Kernel code starts at 0x1000 */
    . = 0x1000;

    .text ALIGN(4) : {
        *(.multiboot)   /* If you ever add a multiboot header */
        *(.text)
    }

    .rodata ALIGN(4) : { *(.rodata) }

    .data ALIGN(4) : { *(.data) }

    .bss ALIGN(4) : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
}