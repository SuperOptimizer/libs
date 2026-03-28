/*
 * cx_hash.h - Hash functions: CRC32, FNV-1a, MurmurHash3.
 * Part of libcx. Pure C89.
 */

#ifndef CX_HASH_H
#define CX_HASH_H

#include <stddef.h>

unsigned long cx_crc32(const void *data, size_t len);
unsigned long cx_fnv1a(const void *data, size_t len);
unsigned long cx_murmur3(const void *data, size_t len, unsigned long seed);

#endif
