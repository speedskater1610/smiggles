[BITS 32]
[GLOBAL _start]
[EXTERN kernel_main]

[GLOBAL irq0_timer_handler]
[GLOBAL irq1_keyboard_handler]
[GLOBAL load_idt]

[EXTERN timer_handler]
[EXTERN keyboard_handler]

_start:
    mov esp, 0x90000
    call kernel_main
    cli
    hlt

; Timer IRQ0 handler stub
irq0_timer_handler:
    pusha
    call timer_handler
    popa
    iretd

; Keyboard IRQ1 handler stub
irq1_keyboard_handler:
    pusha
    call keyboard_handler
    popa
    iretd

; Load IDT routine
load_idt:
    mov eax, [esp+4] ; pointer to IDT ptr
    lidt [eax]
    ret
