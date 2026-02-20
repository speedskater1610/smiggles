Note: New Build commands:

qemu-img create -f raw myos/hdd.img 10M   - creating a new hard disk (hdd.img)
make -C myos   - building the files
qemu-system-i386 -fda myos/floppy.img -hda myos/hdd.img -serial stdio   - runs smiggles