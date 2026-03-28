/*
 * hash.c - Hash function implementations: CRC32, FNV-1a, MurmurHash3.
 * Part of libcx. Pure C89.
 */

#include "cx_hash.h"

/* CRC32 with pre-computed table */
static unsigned long crc32_table[256];
static int crc32_table_init = 0;

static void crc32_init_table(void)
{
    unsigned long i, j, crc;
    for (i = 0; i < 256; i++) {
        crc = i;
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_init = 1;
}

unsigned long cx_crc32(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    unsigned long crc = 0xFFFFFFFFUL;
    size_t i;

    if (!crc32_table_init) {
        crc32_init_table();
    }

    for (i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}

unsigned long cx_fnv1a(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    unsigned long h = 2166136261UL;
    size_t i;

    for (i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619UL;
    }
    return h;
}

static unsigned long rotl32(unsigned long x, int r)
{
    return ((x << r) | (x >> (32 - r))) & 0xFFFFFFFFUL;
}

unsigned long cx_murmur3(const void *data, size_t len, unsigned long seed)
{
    const unsigned char *p = (const unsigned char *)data;
    unsigned long h = seed & 0xFFFFFFFFUL;
    unsigned long k;
    size_t nblocks;
    size_t i;
    size_t tail_idx;
    unsigned long c1 = 0xCC9E2D51UL;
    unsigned long c2 = 0x1B873593UL;

    nblocks = len / 4;

    /* Body */
    for (i = 0; i < nblocks; i++) {
        size_t off = i * 4;
        k = (unsigned long)p[off] |
            ((unsigned long)p[off + 1] << 8) |
            ((unsigned long)p[off + 2] << 16) |
            ((unsigned long)p[off + 3] << 24);

        k = (k * c1) & 0xFFFFFFFFUL;
        k = rotl32(k, 15);
        k = (k * c2) & 0xFFFFFFFFUL;

        h ^= k;
        h = rotl32(h, 13);
        h = (h * 5 + 0xE6546B64UL) & 0xFFFFFFFFUL;
    }

    /* Tail */
    tail_idx = nblocks * 4;
    k = 0;

    switch (len & 3) {
    case 3: k ^= (unsigned long)p[tail_idx + 2] << 16; /* fallthrough */
    case 2: k ^= (unsigned long)p[tail_idx + 1] << 8;  /* fallthrough */
    case 1: k ^= (unsigned long)p[tail_idx];
            k = (k * c1) & 0xFFFFFFFFUL;
            k = rotl32(k, 15);
            k = (k * c2) & 0xFFFFFFFFUL;
            h ^= k;
    }

    /* Finalization */
    h ^= (unsigned long)len;
    h ^= h >> 16;
    h = (h * 0x85EBCA6BUL) & 0xFFFFFFFFUL;
    h ^= h >> 13;
    h = (h * 0xC2B2AE35UL) & 0xFFFFFFFFUL;
    h ^= h >> 16;

    return h;
}
