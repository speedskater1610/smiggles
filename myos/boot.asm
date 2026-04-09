; Legacy placeholder.
;
; Smiggles now boots through GRUB2 (Multiboot2), not a custom 16-bit boot
; sector. The active boot path is:
;   1) GRUB loads kernel.elf
;   2) GRUB jumps to _start in kernel_entry.asm
;   3) kernel_main receives Multiboot2 memory map data
;
; This file is intentionally kept for historical reference and is not used by
; the Makefile anymore.
