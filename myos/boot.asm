; boot.asm
[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov si, msg
.print:
    lodsb
    or al, al
    jz load_kernel
    mov ah, 0x0E
    int 0x10
    jmp .print

load_kernel:
    mov ah, 0x02        ; BIOS read sector
    mov al, 4           ; read 4 sectors (adjust if needed)
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, 0x80
    mov bx, 0x1000
    int 0x13

    ; switch to protected mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode_entry

gdt_start:
    dd 0
    dd 0
    dd 0x0000FFFF
    dd 0x00CF9A00      ; code segment
    dd 0
    dd 0x00CF9200      ; data segment
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[BITS 32]
protected_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00

    jmp 0x1000          ; jump directly to C kernel address

hang:
    cli
    hlt
    jmp hang

msg db "Bootloader OK", 0

times 510-($-$$) db 0
dw 0xAA55
