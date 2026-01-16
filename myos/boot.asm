[BITS 16]
[ORG 0x7C00]

start:
    mov si, message

print:
    lodsb
    or al, al
    jz done
    mov ah, 0x0E
    int 0x10
    jmp print

done:
    cli
    hlt

message db "Smiggles", 0

times 510-($-$$) db 0
dw 0xAA55
