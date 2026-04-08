# Bootloader

BIOS reads one sector (512 bytes) from disk into RAM at address 0x7C00. This sector, the bootloader, sets up segment registers and stack so code can run safely. Next, the bootloader loads additional  sectors into RAM at address 0x10000. The number of sectors KERNEL_SECTORS is dynamic (generated from kernel size in the Makefile at build time).Then, the bootloader loads a GDT (global descriptor table), sets the protected mode bit in register CR0, then performs a far jump into 32-bit (protected) mode. This transfers control to the kernel entry code, which sets up interrupts and core runtime support, then calls the main kernel function. This starts up the shell.

