#include "kernel.h"

#define RTL8139_IDR0      0x00
#define RTL8139_RBSTART   0x30
#define RTL8139_CR        0x37
#define RTL8139_CAPR      0x38
#define RTL8139_CBR       0x3A
#define RTL8139_IMR       0x3C
#define RTL8139_ISR       0x3E
#define RTL8139_TSD0      0x10
#define RTL8139_TCR       0x40
#define RTL8139_RCR       0x44
#define RTL8139_CONFIG1   0x52
#define RTL8139_MPC       0x4C
#define RTL8139_TSAD0     0x20

#define RTL8139_CR_RST    0x10
#define RTL8139_CR_RE     0x08
#define RTL8139_CR_TE     0x04
#define RTL8139_CR_BUFE   0x01

#define RTL8139_RX_BUFFER_SIZE 8192
#define RTL8139_RX_EXTRA       (16 + 1500)
#define RTL8139_TX_BUFFER_SIZE 1792
#define RTL8139_TX_BUFFER_COUNT 4

#define RTL8139_ISR_ROK   0x0001u
#define RTL8139_ISR_TOK   0x0004u

#define RTL8139_RX_STATUS_ROK 0x0001u

#define RTL8139_RCR_AAP    0x00000001u
#define RTL8139_RCR_APM    0x00000002u
#define RTL8139_RCR_AM     0x00000004u
#define RTL8139_RCR_AB     0x00000008u
#define RTL8139_RCR_WRAP   0x00000080u
#define RTL8139_RCR_RBLEN_8K 0x00000000u

static unsigned char rtl_rx_buffer[RTL8139_RX_BUFFER_SIZE + RTL8139_RX_EXTRA] __attribute__((aligned(256)));
static unsigned char rtl_tx_buffer[RTL8139_TX_BUFFER_COUNT][RTL8139_TX_BUFFER_SIZE] __attribute__((aligned(16)));
static Rtl8139Status rtl_status;
static uint32_t rtl_tx_index = 0;
static uint32_t rtl_rx_offset = 0;

static void rtl_copy_bytes(uint8_t* dst, const uint8_t* src, int count) {
    for (int i = 0; i < count; i++) dst[i] = src[i];
}

static uint16_t rtl_read_u16(const unsigned char* ptr) {
    return (uint16_t)((uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8));
}

static void rtl_delay(void) {
    for (volatile int i = 0; i < 10000; i++) {
    }
}

static void rtl_copy_mac_text(char* out, const uint8_t mac[6]) {
    const char* hex = "0123456789ABCDEF";
    int pos = 0;

    for (int i = 0; i < 6; i++) {
        if (i > 0) out[pos++] = ':';
        out[pos++] = hex[(mac[i] >> 4) & 0x0F];
        out[pos++] = hex[mac[i] & 0x0F];
    }
    out[pos] = 0;
}

static void rtl_reset_status(void) {
    rtl_status.present = 0;
    rtl_status.initialized = 0;
    rtl_status.io_base = 0;
    rtl_status.irq_line = 0;
    rtl_status.tx_packets = 0;
    rtl_status.rx_packets = 0;
    rtl_status.last_rx_length = 0;
    for (int i = 0; i < 6; i++) rtl_status.mac[i] = 0;

    rtl_tx_index = 0;
    rtl_rx_offset = 0;
}

int rtl8139_init(void) {
    PciRtl8139Info pci_info;

    rtl_reset_status();

    if (!pci_find_rtl8139(&pci_info)) {
        return 0;
    }

    rtl_status.present = 1;
    rtl_status.io_base = pci_info.io_base;
    rtl_status.irq_line = pci_info.irq_line;

    if (pci_info.io_base == 0) {
        return -2;
    }

    if (!pci_enable_device_io_busmaster(pci_info.bus, pci_info.slot, pci_info.function)) {
        return -3;
    }

    outb((unsigned short)(pci_info.io_base + RTL8139_CONFIG1), 0x00);
    outb((unsigned short)(pci_info.io_base + RTL8139_CR), RTL8139_CR_RST);

    for (int i = 0; i < 100000; i++) {
        if ((inb((unsigned short)(pci_info.io_base + RTL8139_CR)) & RTL8139_CR_RST) == 0) {
            break;
        }
        rtl_delay();
        if (i == 99999) {
            return -4;
        }
    }

    for (int i = 0; i < 6; i++) {
        rtl_status.mac[i] = inb((unsigned short)(pci_info.io_base + RTL8139_IDR0 + i));
    }

    outw((unsigned short)(pci_info.io_base + RTL8139_IMR), 0x0000);
    outw((unsigned short)(pci_info.io_base + RTL8139_ISR), 0xFFFF);
    outl((unsigned short)(pci_info.io_base + RTL8139_RBSTART), (unsigned int)rtl_rx_buffer);

    for (int i = 0; i < RTL8139_TX_BUFFER_COUNT; i++) {
        outl((unsigned short)(pci_info.io_base + RTL8139_TSAD0 + (i * 4)), (unsigned int)rtl_tx_buffer[i]);
    }

    outl((unsigned short)(pci_info.io_base + RTL8139_TCR), 0x00000000u);
    outl((unsigned short)(pci_info.io_base + RTL8139_RCR), RTL8139_RCR_AAP | RTL8139_RCR_APM | RTL8139_RCR_AM | RTL8139_RCR_AB | RTL8139_RCR_WRAP | RTL8139_RCR_RBLEN_8K);
    outw((unsigned short)(pci_info.io_base + RTL8139_MPC), 0x0000);
    // CAPR must start at 0xFFF0 (ring offset - 16) for an empty RX ring.
    outw((unsigned short)(pci_info.io_base + RTL8139_CAPR), 0xFFF0);
    outw((unsigned short)(pci_info.io_base + RTL8139_ISR), 0xFFFF);
    outb((unsigned short)(pci_info.io_base + RTL8139_CR), RTL8139_CR_RE | RTL8139_CR_TE);

    rtl_status.initialized = 1;
    return 1;
}

int rtl8139_get_status(Rtl8139Status* out_status) {
    if (!out_status) return 0;
    *out_status = rtl_status;
    return 1;
}

int rtl8139_send_frame(const uint8_t* frame, int length) {
    uint32_t io_base = rtl_status.io_base;
    uint32_t slot = rtl_tx_index;

    if (!rtl_status.initialized) return -1;
    if (!frame || length <= 0) return -2;
    if (length > RTL8139_TX_BUFFER_SIZE) return -3;

    rtl_copy_bytes(rtl_tx_buffer[slot], frame, length);
    outl((unsigned short)(io_base + RTL8139_TSD0 + (slot * 4)), (uint32_t)length);

    rtl_tx_index = (rtl_tx_index + 1) % RTL8139_TX_BUFFER_COUNT;
    rtl_status.tx_packets++;

    return length;
}

int rtl8139_poll_receive(uint8_t* frame_out, int max_length, int* out_length) {
    uint32_t io_base = rtl_status.io_base;
    uint32_t packet_len;
    uint16_t packet_status;
    uint16_t cbr;
    unsigned char* packet_header;
    int copy_len;

    if (out_length) *out_length = 0;
    if (!rtl_status.initialized) return -1;
    if (!frame_out || max_length <= 0) return -2;

    if (inb((unsigned short)(io_base + RTL8139_CR)) & RTL8139_CR_BUFE) {
        return 0;
    }

    cbr = inw((unsigned short)(io_base + RTL8139_CBR));
    if (cbr == (uint16_t)rtl_rx_offset) {
        return 0;
    }

    packet_header = &rtl_rx_buffer[rtl_rx_offset];
    packet_status = rtl_read_u16(packet_header);
    packet_len = rtl_read_u16(packet_header + 2);

    if ((packet_status & RTL8139_RX_STATUS_ROK) == 0 || packet_len == 0 || packet_len > (RTL8139_RX_BUFFER_SIZE + RTL8139_RX_EXTRA)) {
        // Treat malformed/empty descriptors as "no packet" to keep polling safe.
        return 0;
    }

    copy_len = (int)packet_len;
    if (copy_len > max_length) copy_len = max_length;

    for (int i = 0; i < copy_len; i++) {
        frame_out[i] = rtl_rx_buffer[(rtl_rx_offset + 4 + i) % RTL8139_RX_BUFFER_SIZE];
    }

    rtl_rx_offset = (rtl_rx_offset + packet_len + 4 + 3) & ~3u;
    rtl_rx_offset %= RTL8139_RX_BUFFER_SIZE;
    outw((unsigned short)(io_base + RTL8139_CAPR), (uint16_t)((rtl_rx_offset - 16) & 0xFFFFu));
    outw((unsigned short)(io_base + RTL8139_ISR), RTL8139_ISR_ROK);

    rtl_status.rx_packets++;
    rtl_status.last_rx_length = packet_len;
    if (out_length) *out_length = copy_len;

    return 1;
}

void rtl8139_print_status(char* video, int* cursor) {
    Rtl8139Status status;
    char line[96];
    char value[32];

    if (!rtl8139_get_status(&status)) {
        print_string("RTL8139: status unavailable", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    if (!status.present) {
        print_string("RTL8139: device not present", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    line[0] = 0;
    str_concat(line, "RTL8139: ");
    str_concat(line, status.initialized ? "ready" : "detected");
    str_concat(line, " io=");

    const char* hex = "0123456789ABCDEF";
    value[0] = '0';
    value[1] = 'x';
    for (int i = 0; i < 8; i++) {
        int shift = (7 - i) * 4;
        value[2 + i] = hex[(status.io_base >> shift) & 0x0F];
    }
    value[10] = 0;
    str_concat(line, value);
    str_concat(line, " irq=");
    int_to_str((int)status.irq_line, value);
    str_concat(line, value);
    print_string(line, -1, video, cursor, status.initialized ? COLOR_LIGHT_GREEN : COLOR_YELLOW);

    rtl_copy_mac_text(value, status.mac);
    line[0] = 0;
    str_concat(line, "MAC: ");
    str_concat(line, value);
    print_string(line, -1, video, cursor, COLOR_LIGHT_CYAN);

    line[0] = 0;
    str_concat(line, "Packets tx=");
    int_to_str((int)status.tx_packets, value);
    str_concat(line, value);
    str_concat(line, " rx=");
    int_to_str((int)status.rx_packets, value);
    str_concat(line, value);
    str_concat(line, " last_rx=");
    int_to_str((int)status.last_rx_length, value);
    str_concat(line, value);
    print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
}
