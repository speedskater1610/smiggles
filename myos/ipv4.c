#include "kernel.h"

#define ETH_TYPE_IPV4_HI 0x08
#define ETH_TYPE_IPV4_LO 0x00

static IPv4Stats ipv4_stats;

static uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint16_t ipv4_header_checksum(const uint8_t* header, int header_len) {
    uint32_t sum = 0;

    for (int i = 0; i < header_len; i += 2) {
        uint16_t word = (uint16_t)((header[i] << 8) | header[i + 1]);
        sum += word;
        while (sum > 0xFFFFu) {
            sum = (sum & 0xFFFFu) + (sum >> 16);
        }
    }

    return (uint16_t)(~sum);
}

int ipv4_poll_once(void) {
    uint8_t frame[1600];
    int length = 0;
    int rx_result = rtl8139_poll_receive(frame, sizeof(frame), &length);

    if (rx_result <= 0) return rx_result;

    ipv4_stats.frames_polled++;

    if (length < 14) {
        ipv4_stats.non_ipv4_frames++;
        return 2;
    }

    if (frame[12] != ETH_TYPE_IPV4_HI || frame[13] != ETH_TYPE_IPV4_LO) {
        ipv4_stats.non_ipv4_frames++;
        return 2;
    }

    if (length < 34) {
        ipv4_stats.bad_total_length++;
        return -4;
    }

    {
        const uint8_t* ip = frame + 14;
        uint8_t version = (uint8_t)(ip[0] >> 4);
        uint8_t ihl_words = (uint8_t)(ip[0] & 0x0F);
        uint16_t total_len = read_be16(ip + 2);
        int header_len = (int)ihl_words * 4;

        if (version != 4) {
            ipv4_stats.bad_version++;
            return -1;
        }

        if (ihl_words < 5 || header_len > 60) {
            ipv4_stats.bad_ihl++;
            return -2;
        }

        if ((14 + header_len) > length || total_len < (uint16_t)header_len || (14 + total_len) > length) {
            ipv4_stats.bad_total_length++;
            return -3;
        }

        if (ipv4_header_checksum(ip, header_len) != 0) {
            ipv4_stats.bad_checksum++;
            return -5;
        }

        ipv4_stats.ipv4_parsed++;
        ipv4_stats.last_protocol = ip[9];
        ipv4_stats.last_ttl = ip[8];
        ipv4_stats.last_total_length = total_len;
        for (int i = 0; i < 4; i++) {
            ipv4_stats.last_src_ip[i] = ip[12 + i];
            ipv4_stats.last_dst_ip[i] = ip[16 + i];
        }

        return 1;
    }
}

int ipv4_get_stats(IPv4Stats* out_stats) {
    if (!out_stats) return 0;
    *out_stats = ipv4_stats;
    return 1;
}
