// hash.c: Simple password hashing (FNV-1a 256-bit variant)
#include "kernel.h"
#include <stdint.h>

void hash_password(const char* password, unsigned char* out_hash) {
    // FNV-1a 256-bit variant (not cryptographically strong, but better than plaintext)
    static const uint64_t FNV_OFFSET[4] = {
        0xCBF29CE484222325ULL, 0x100000001B3ULL, 0xC6A4A7935BD1E995ULL, 0xD6E8FEB86659FD93ULL
    };
    static const uint64_t FNV_PRIME = 0x100000001B3ULL;
    uint64_t hash[4];
    for (int i = 0; i < 4; i++) hash[i] = FNV_OFFSET[i];
    for (const char* p = password; *p; p++) {
        for (int i = 0; i < 4; i++) {
            hash[i] ^= (unsigned char)(*p);
            hash[i] *= FNV_PRIME;
        }
    }
    for (int i = 0; i < 4; i++) {
        out_hash[i*8+0] = (hash[i] >>  0) & 0xFF;
        out_hash[i*8+1] = (hash[i] >>  8) & 0xFF;
        out_hash[i*8+2] = (hash[i] >> 16) & 0xFF;
        out_hash[i*8+3] = (hash[i] >> 24) & 0xFF;
        out_hash[i*8+4] = (hash[i] >> 32) & 0xFF;
        out_hash[i*8+5] = (hash[i] >> 40) & 0xFF;
        out_hash[i*8+6] = (hash[i] >> 48) & 0xFF;
        out_hash[i*8+7] = (hash[i] >> 56) & 0xFF;
    }
}
