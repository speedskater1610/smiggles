#include "kernel.h"

#define PCI_CONFIG_ADDRESS_PORT 0xCF8
#define PCI_CONFIG_DATA_PORT    0xCFC

#define PCI_INVALID_VENDOR 0xFFFFu
#define RTL8139_VENDOR_ID  0x10ECu
#define RTL8139_DEVICE_ID  0x8139u

static unsigned int pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    unsigned int address =
        (1u << 31) |
        ((unsigned int)bus << 16) |
        ((unsigned int)slot << 11) |
        ((unsigned int)function << 8) |
        ((unsigned int)(offset & 0xFC));

    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

static void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    unsigned int address =
        (1u << 31) |
        ((unsigned int)bus << 16) |
        ((unsigned int)slot << 11) |
        ((unsigned int)function << 8) |
        ((unsigned int)(offset & 0xFC));

    outl(PCI_CONFIG_ADDRESS_PORT, address);
    outl(PCI_CONFIG_DATA_PORT, value);
}

static uint16_t pci_vendor_id(uint8_t bus, uint8_t slot, uint8_t function) {
    return (uint16_t)(pci_config_read32(bus, slot, function, 0x00) & 0xFFFFu);
}

static uint16_t pci_device_id(uint8_t bus, uint8_t slot, uint8_t function) {
    return (uint16_t)((pci_config_read32(bus, slot, function, 0x00) >> 16) & 0xFFFFu);
}

static uint8_t pci_header_type(uint8_t bus, uint8_t slot, uint8_t function) {
    return (uint8_t)((pci_config_read32(bus, slot, function, 0x0C) >> 16) & 0xFFu);
}

int pci_find_rtl8139(PciRtl8139Info* out_info) {
    if (!out_info) return 0;

    out_info->found = 0;
    out_info->bus = 0;
    out_info->slot = 0;
    out_info->function = 0;
    out_info->vendor_id = 0;
    out_info->device_id = 0;
    out_info->io_base = 0;
    out_info->irq_line = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor0 = pci_vendor_id((uint8_t)bus, slot, 0);
            if (vendor0 == PCI_INVALID_VENDOR) continue;

            uint8_t max_functions = 1;
            if (pci_header_type((uint8_t)bus, slot, 0) & 0x80) {
                max_functions = 8;
            }

            for (uint8_t function = 0; function < max_functions; function++) {
                uint16_t vendor = pci_vendor_id((uint8_t)bus, slot, function);
                if (vendor == PCI_INVALID_VENDOR) continue;

                uint16_t device = pci_device_id((uint8_t)bus, slot, function);
                if (vendor == RTL8139_VENDOR_ID && device == RTL8139_DEVICE_ID) {
                    unsigned int bar0 = pci_config_read32((uint8_t)bus, slot, function, 0x10);
                    unsigned int irq = pci_config_read32((uint8_t)bus, slot, function, 0x3C);

                    out_info->found = 1;
                    out_info->bus = (uint8_t)bus;
                    out_info->slot = slot;
                    out_info->function = function;
                    out_info->vendor_id = vendor;
                    out_info->device_id = device;
                    out_info->io_base = (bar0 & 0x1u) ? (bar0 & ~0x3u) : 0;
                    out_info->irq_line = (uint8_t)(irq & 0xFFu);
                    return 1;
                }
            }
        }
    }

    return 0;
}

int pci_enable_device_io_busmaster(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t command_reg = pci_config_read32(bus, slot, function, 0x04);
    uint32_t updated = command_reg | 0x00000005u;

    if (updated != command_reg) {
        pci_config_write32(bus, slot, function, 0x04, updated);
        command_reg = pci_config_read32(bus, slot, function, 0x04);
    }

    return ((command_reg & 0x00000005u) == 0x00000005u) ? 1 : 0;
}

static void append_u32_hex(char* out, uint32_t value) {
    const char* hex = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; i++) {
        int shift = (7 - i) * 4;
        out[2 + i] = hex[(value >> shift) & 0xFu];
    }
    out[10] = 0;
}

static void append_u8_dec(char* out, uint8_t value) {
    int pos = 0;
    if (value >= 100) {
        out[pos++] = (char)('0' + (value / 100));
        out[pos++] = (char)('0' + ((value / 10) % 10));
        out[pos++] = (char)('0' + (value % 10));
    } else if (value >= 10) {
        out[pos++] = (char)('0' + (value / 10));
        out[pos++] = (char)('0' + (value % 10));
    } else {
        out[pos++] = (char)('0' + value);
    }
    out[pos] = 0;
}

void pci_scan_and_print(char* video, int* cursor) {
    char line[96];
    PciRtl8139Info info;

    if (!pci_find_rtl8139(&info)) {
        print_string("PCI: RTL8139 not found", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    line[0] = 0;
    str_concat(line, "PCI: RTL8139 b");

    char tmp[16];
    append_u8_dec(tmp, info.bus);
    str_concat(line, tmp);

    str_concat(line, ":s");
    append_u8_dec(tmp, info.slot);
    str_concat(line, tmp);

    str_concat(line, ":f");
    append_u8_dec(tmp, info.function);
    str_concat(line, tmp);

    str_concat(line, " io=");
    append_u32_hex(tmp, info.io_base);
    str_concat(line, tmp);

    str_concat(line, " irq=");
    append_u8_dec(tmp, info.irq_line);
    str_concat(line, tmp);

    print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
}
