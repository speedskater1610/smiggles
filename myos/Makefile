ASM=nasm
CC=i686-elf-gcc
LD=i686-elf-ld
QEMU=qemu-system-i386
HOST_UDP_PORT?=
HOST_HTTP_PORT?=

QEMU_USERNET=-net user
ifneq ($(strip $(HOST_UDP_PORT)),)
QEMU_USERNET+=,hostfwd=udp::$(HOST_UDP_PORT)-:5555
endif
ifneq ($(strip $(HOST_HTTP_PORT)),)
QEMU_USERNET+=,hostfwd=tcp::$(HOST_HTTP_PORT)-:8080
endif

GRUB_MKRESCUE ?= $(shell command -v grub-mkrescue 2>/dev/null || command -v i686-elf-grub-mkrescue 2>/dev/null)
GRUB_FILE ?= $(shell command -v grub-file 2>/dev/null || command -v i686-elf-grub-file 2>/dev/null)

CFLAGS=-ffreestanding -m32 -O2 -Wall -Wextra
LDFLAGS=-T linker.ld -nostdlib

KERNEL_OBJS= \
	kernel_entry.o kernel.o interrupts.o memory.o filesystem.o display.o \
	calculator.o basic.o editor.o commands.o clipboard.o diskio.o ata.o \
	process.o syscall.o protection.o pci.o rtl8139.o arp.o ipv4.o icmp.o \
	udp.o net.o tcp.o sockets.o hash.o logger.o

all: smiggles.iso hdd.img

kernel_entry.o: kernel_entry.asm
	$(ASM) -f elf32 kernel_entry.asm -o kernel_entry.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

interrupts.o: interrupts.c
	$(CC) $(CFLAGS) -c interrupts.c -o interrupts.o

memory.o: memory.c
	$(CC) $(CFLAGS) -c memory.c -o memory.o

filesystem.o: filesystem.c
	$(CC) $(CFLAGS) -c filesystem.c -o filesystem.o

display.o: display.c
	$(CC) $(CFLAGS) -c display.c -o display.o

calculator.o: calculator.c
	$(CC) $(CFLAGS) -c calculator.c -o calculator.o

basic.o: basic.c
	$(CC) $(CFLAGS) -c basic.c -o basic.o

hash.o: hash.c
	$(CC) $(CFLAGS) -c hash.c -o hash.o

logger.o: logger.c
	$(CC) $(CFLAGS) -c logger.c -o logger.o

editor.o: editor.c
	$(CC) $(CFLAGS) -c editor.c -o editor.o

commands.o: commands.c
	$(CC) $(CFLAGS) -c commands.c -o commands.o

clipboard.o: clipboard.c
	$(CC) $(CFLAGS) -c clipboard.c -o clipboard.o

diskio.o: diskio.c
	$(CC) $(CFLAGS) -c diskio.c -o diskio.o

ata.o: ata.c
	$(CC) $(CFLAGS) -c ata.c -o ata.o

process.o: process.c
	$(CC) $(CFLAGS) -c process.c -o process.o

syscall.o: syscall.c
	$(CC) $(CFLAGS) -c syscall.c -o syscall.o

protection.o: protection.c
	$(CC) $(CFLAGS) -c protection.c -o protection.o

pci.o: pci.c
	$(CC) $(CFLAGS) -c pci.c -o pci.o

rtl8139.o: rtl8139.c
	$(CC) $(CFLAGS) -c rtl8139.c -o rtl8139.o

arp.o: arp.c
	$(CC) $(CFLAGS) -c arp.c -o arp.o

ipv4.o: ipv4.c
	$(CC) $(CFLAGS) -c ipv4.c -o ipv4.o

icmp.o: icmp.c
	$(CC) $(CFLAGS) -c icmp.c -o icmp.o

udp.o: udp.c
	$(CC) $(CFLAGS) -c udp.c -o udp.o

net.o: net.c
	$(CC) $(CFLAGS) -c net.c -o net.o

tcp.o: tcp.c
	$(CC) $(CFLAGS) -c tcp.c -o tcp.o

sockets.o: sockets.c
	$(CC) $(CFLAGS) -c sockets.c -o sockets.o

kernel.elf: $(KERNEL_OBJS) linker.ld
	$(LD) $(LDFLAGS) $(KERNEL_OBJS) -o kernel.elf
	@if [ -n "$(GRUB_FILE)" ]; then \
		$(GRUB_FILE) --is-x86-multiboot2 kernel.elf || { echo "kernel.elf is not multiboot2-compliant"; exit 1; }; \
	fi

iso/boot/grub/grub.cfg:
	@mkdir -p iso/boot/grub
	@printf 'set timeout=0\nset default=0\nterminal_output console\ninsmod multiboot2\nmultiboot2 /boot/kernel.elf\nboot\n' > iso/boot/grub/grub.cfg

smiggles.iso: kernel.elf iso/boot/grub/grub.cfg
	@if [ -z "$(GRUB_MKRESCUE)" ]; then \
		echo "grub-mkrescue (or i686-elf-grub-mkrescue) is required"; \
		exit 1; \
	fi
	@mkdir -p iso/boot
	cp kernel.elf iso/boot/kernel.elf
	$(GRUB_MKRESCUE) -o smiggles.iso iso >/dev/null

hdd.img:
	@if [ ! -f hdd.img ]; then qemu-img create -f raw hdd.img 10M; fi

run: smiggles.iso hdd.img
	$(QEMU) -drive file=smiggles.iso,media=cdrom,format=raw,if=none,id=cdrom -device ide-cd,drive=cdrom,bus=ide.1 \
	      -drive file=hdd.img,format=raw,if=none,id=hdd -device ide-hd,drive=hdd,bus=ide.0 \
	      -boot d -serial stdio -net nic,model=rtl8139 $(QEMU_USERNET)

clean:
	rm -f *.o *.bin *.elf *.img *.iso qemu*.log
	rm -rf iso

.PHONY: all run clean
