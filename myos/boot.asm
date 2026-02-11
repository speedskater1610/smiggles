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
.loop:
    lodsb
    or al, al
    jz load_kernel
    mov ah, 0x0E
    int 0x10
    jmp .loop

load_kernel:
    mov dl, 0x00
    mov ah, 0x00
    int 0x13
    
    ; Load the kernel starting at physical address 0x0010000.
    ; Use ES:BX = 0x1000:0x0000 => 0x0010000.
    mov ax, 0x1000
    mov es, ax

    mov ah, 0x02
    ; NOTE: This must be >= ceil(kernel.bin_size / 512).
    ; kernel.bin is currently ~28 KiB, so 64 sectors (~32 KiB) is safe.
    mov al, 64
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov bx, 0x0000
    int 0x13
    jc disk_error

    lgdt [gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:protected_mode

disk_error:
    mov si, err
.loop_err:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    int 0x10
    jmp .loop_err
.halt:
    hlt

gdt_start:
    dq 0x0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_desc:
    dw 24
    dd gdt_start

[BITS 32]
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    ; Jump to kernel entry point at 0x0010000
    jmp 0x10000

msg db "Bootloader OK",13,10,0
err db "Disk error",13,10,0

times 510-($-$$) db 0
dw 0xAA55
