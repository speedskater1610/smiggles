ENTRY(_start)
SECTIONS
{
    /* Kernel code starts at 0x1000 */
    . = 0x1000;

    .text ALIGN(4) : {
        *(.multiboot)
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

    /* Reserve 64 KB for the stack at 0x90000 */
    . = 0x90000;
    stack_top = .;
    . = . + 0x10000; /* 64 KB stack */
    stack_bottom = .;
}