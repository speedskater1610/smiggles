// diskio.c: BIOS disk I/O routines for persistent storage
#include "kernel.h"

// Use ATA PIO for real persistent storage
int disk_read_sector(unsigned int lba, void* buffer) {
    return ata_read_sector(lba, buffer);
}
int disk_write_sector(unsigned int lba, const void* buffer) {
    return ata_write_sector(lba, buffer);
}
