ASM = nasm
CC  = i686-elf-gcc
LD  = i686-elf-ld

CFLAGS  = -ffreestanding -m32 -O2 -Wall -Wextra -MMD -MP
LDFLAGS = -T linker.ld -nostdlib

QEMU ?= $(shell command -v qemu-system-i386 2>/dev/null || command -v qemu-system-i386.exe 2>/dev/null)
GRUB_MKRESCUE ?= $(shell command -v grub-mkrescue 2>/dev/null || command -v i686-elf-grub-mkrescue 2>/dev/null)
GRUB_FILE ?= $(shell command -v grub-file 2>/dev/null || command -v i686-elf-grub-file 2>/dev/null)

# Sources
C_SOURCES = \
	kernel.c interrupts.c memory.c filesystem.c display.c \
	calculator.c basic.c editor.c commands.c clipboard.c \
	diskio.c ata.c process.c syscall.c protection.c \
	pci.c rtl8139.c arp.c ipv4.c icmp.c udp.c net.c \
	tcp.c sockets.c hash.c logger.c

ASM_SOURCES = kernel_entry.asm

# Objects
OBJS = $(C_SOURCES:.c=.o) $(ASM_SOURCES:.asm=.o)

# Dependency files (.d)
DEPS = $(C_SOURCES:.c=.d)

all: smiggles.iso hdd.img

# Pattern rules
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(ASM) -f elf32 $< -o $@

# Link
kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@
	@if [ -n "$(GRUB_FILE)" ]; then \
		$(GRUB_FILE) --is-x86-multiboot2 $@ || { echo "Not multiboot2!"; exit 1; }; \
	fi

# GRUB config
iso/boot/grub/grub.cfg:
	mkdir -p iso/boot/grub
	printf 'set timeout=0\nset default=0\nterminal_output console\ninsmod multiboot2\nmultiboot2 /boot/kernel.elf\nboot\n' > $@

# ISO
smiggles.iso: kernel.elf iso/boot/grub/grub.cfg
	mkdir -p iso/boot
	cp kernel.elf iso/boot/kernel.elf
	$(GRUB_MKRESCUE) -o $@ iso >/dev/null

# HDD
hdd.img:
	@if [ ! -f $@ ]; then qemu-img create -f raw $@ 10M; fi

# Run
run: smiggles.iso hdd.img
	$(QEMU) \
	-drive file=smiggles.iso,media=cdrom \
	-drive file=hdd.img,format=raw \
	-boot d -serial stdio \
	-net nic,model=rtl8139 -net user

# Clean
clean:
	rm -f *.o *.d *.elf *.iso *.img
	rm -rf iso

-include $(DEPS)

.PHONY: all run clean
