// morton.h — Pure C23 Morton code / Z-order curve library
// Single-header: declare API, then #define MT_IMPLEMENTATION in one .c file.
// Cache-friendly iteration over 2D/3D grids for volumetric data processing.
#ifndef MORTON_H
#define MORTON_H

#include <stdbool.h>
#include <stdint.h>

// ──Version ─────────────────────────────────────────────────────────────────

#define MT_VERSION_MAJOR 0
#define MT_VERSION_MINOR 1
#define MT_VERSION_PATCH 0

// ──C23 Compat -------------------------------------------------------------

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define MT_NODISCARD    [[nodiscard]]
  #define MT_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define MT_NODISCARD    __attribute__((warn_unused_result))
  #define MT_MAYBE_UNUSED __attribute__((unused))
#else
  #define MT_NODISCARD
  #define MT_MAYBE_UNUSED
#endif

// ──Linkage ─────────────────────────────────────────────────────────────────

#ifndef MTDEF
  #ifdef MT_STATIC
    #define MTDEF static
  #else
    #define MTDEF extern
  #endif
#endif

// ──2D Morton Codes --------------------------------------------------------

// Encode (x, y) -> morton code. x, y must be < 2^16.
MT_NODISCARD MTDEF uint32_t mt_encode_2d(uint16_t x, uint16_t y);

// Decode morton code -> (x, y).
MTDEF void mt_decode_2d(uint32_t code, uint16_t* x, uint16_t* y);

// 64-bit variants for larger coordinates (x, y < 2^32).
MT_NODISCARD MTDEF uint64_t mt_encode_2d_64(uint32_t x, uint32_t y);

// Decode 64-bit morton code -> (x, y).
MTDEF void mt_decode_2d_64(uint64_t code, uint32_t* x, uint32_t* y);

// ──3D Morton Codes --------------------------------------------------------

// Encode (x, y, z) -> morton code. x, y, z must be < 2^10 (1024).
MT_NODISCARD MTDEF uint32_t mt_encode_3d(uint16_t x, uint16_t y, uint16_t z);

// Decode morton code -> (x, y, z).
MTDEF void mt_decode_3d(uint32_t code, uint16_t* x, uint16_t* y, uint16_t* z);

// 64-bit variants (x, y, z < 2^21).
MT_NODISCARD MTDEF uint64_t mt_encode_3d_64(uint32_t x, uint32_t y, uint32_t z);

// Decode 64-bit morton code -> (x, y, z).
MTDEF void mt_decode_3d_64(uint64_t code, uint32_t* x, uint32_t* y, uint32_t* z);

// ──Iterators --------------------------------------------------------------

// Iterator for 2D Morton-order traversal of a grid.
typedef struct {
    uint32_t code;
    uint16_t x, y;
    uint16_t nx, ny;
    uint32_t remaining;
} mt_iter2d;

// Iterator for 3D Morton-order traversal of a grid.
typedef struct {
    uint32_t code;
    uint16_t x, y, z;
    uint16_t nx, ny, nz;
    uint32_t remaining;
} mt_iter3d;

MT_NODISCARD MTDEF mt_iter2d mt_iter2d_start(uint16_t nx, uint16_t ny);
MT_NODISCARD MTDEF bool      mt_iter2d_valid(const mt_iter2d* it);
MTDEF void                   mt_iter2d_next(mt_iter2d* it);

MT_NODISCARD MTDEF mt_iter3d mt_iter3d_start(uint16_t nx, uint16_t ny, uint16_t nz);
MT_NODISCARD MTDEF bool      mt_iter3d_valid(const mt_iter3d* it);
MTDEF void                   mt_iter3d_next(mt_iter3d* it);

// ──Utility ─────────────────────────────────────────────────────────────────

// Compare two 3D points by their Morton order (for sorting).
MT_NODISCARD MTDEF int mt_compare_3d(uint16_t x1, uint16_t y1, uint16_t z1,
                                     uint16_t x2, uint16_t y2, uint16_t z2);

// Version string.
MT_NODISCARD MTDEF const char* mt_version_str(void);

// ── Implementation ─────────────────────────────────────────────────────────

#ifdef MT_IMPLEMENTATION

// ──Internal: 2D bit spread/compact ----------------------------------------

// Spread 16 bits of x into even bit positions of a 32-bit value.
static inline uint32_t mt__spread_2d_32(uint16_t v) {
    uint32_t x = v;
    x = (x | (x << 8)) & 0x00FF00FFu;
    x = (x | (x << 4)) & 0x0F0F0F0Fu;
    x = (x | (x << 2)) & 0x33333333u;
    x = (x | (x << 1)) & 0x55555555u;
    return x;
}

// Compact even bit positions of a 32-bit value into 16 bits.
static inline uint16_t mt__compact_2d_32(uint32_t x) {
    x &= 0x55555555u;
    x = (x | (x >> 1)) & 0x33333333u;
    x = (x | (x >> 2)) & 0x0F0F0F0Fu;
    x = (x | (x >> 4)) & 0x00FF00FFu;
    x = (x | (x >> 8)) & 0x0000FFFFu;
    return (uint16_t)x;
}

// Spread 32 bits into even bit positions of a 64-bit value.
static inline uint64_t mt__spread_2d_64(uint32_t v) {
    uint64_t x = v;
    x = (x | (x << 16)) & 0x0000FFFF0000FFFFull;
    x = (x | (x <<  8)) & 0x00FF00FF00FF00FFull;
    x = (x | (x <<  4)) & 0x0F0F0F0F0F0F0F0Full;
    x = (x | (x <<  2)) & 0x3333333333333333ull;
    x = (x | (x <<  1)) & 0x5555555555555555ull;
    return x;
}

// Compact even bit positions of a 64-bit value into 32 bits.
static inline uint32_t mt__compact_2d_64(uint64_t x) {
    x &= 0x5555555555555555ull;
    x = (x | (x >>  1)) & 0x3333333333333333ull;
    x = (x | (x >>  2)) & 0x0F0F0F0F0F0F0F0Full;
    x = (x | (x >>  4)) & 0x00FF00FF00FF00FFull;
    x = (x | (x >>  8)) & 0x0000FFFF0000FFFFull;
    x = (x | (x >> 16)) & 0x00000000FFFFFFFFull;
    return (uint32_t)x;
}

// ──Internal: 3D bit spread/compact ----------------------------------------

// Spread 10 bits into every-third bit positions of a 32-bit value.
static inline uint32_t mt__spread_3d_32(uint16_t v) {
    uint32_t x = v & 0x3FFu; // mask to 10 bits
    x = (x | (x << 16)) & 0x030000FFu;
    x = (x | (x <<  8)) & 0x0300F00Fu;
    x = (x | (x <<  4)) & 0x030C30C3u;
    x = (x | (x <<  2)) & 0x09249249u;
    return x;
}

// Compact every-third bit of a 32-bit value into 10 bits.
static inline uint16_t mt__compact_3d_32(uint32_t x) {
    x &= 0x09249249u;
    x = (x | (x >>  2)) & 0x030C30C3u;
    x = (x | (x >>  4)) & 0x0300F00Fu;
    x = (x | (x >>  8)) & 0x030000FFu;
    x = (x | (x >> 16)) & 0x000003FFu;
    return (uint16_t)x;
}

// Spread 21 bits into every-third bit positions of a 64-bit value.
static inline uint64_t mt__spread_3d_64(uint32_t v) {
    uint64_t x = v & 0x1FFFFFull; // mask to 21 bits
    x = (x | (x << 32)) & 0x1F00000000FFFFull;
    x = (x | (x << 16)) & 0x1F0000FF0000FFull;
    x = (x | (x <<  8)) & 0x100F00F00F00F00Full;
    x = (x | (x <<  4)) & 0x10C30C30C30C30C3ull;
    x = (x | (x <<  2)) & 0x1249249249249249ull;
    return x;
}

// Compact every-third bit of a 64-bit value into 21 bits.
static inline uint32_t mt__compact_3d_64(uint64_t x) {
    x &= 0x1249249249249249ull;
    x = (x | (x >>  2)) & 0x10C30C30C30C30C3ull;
    x = (x | (x >>  4)) & 0x100F00F00F00F00Full;
    x = (x | (x >>  8)) & 0x1F0000FF0000FFull;
    x = (x | (x >> 16)) & 0x1F00000000FFFFull;
    x = (x | (x >> 32)) & 0x1FFFFFull;
    return (uint32_t)x;
}

// ──2D Encode/Decode -------------------------------------------------------

MTDEF uint32_t mt_encode_2d(uint16_t x, uint16_t y) {
    return mt__spread_2d_32(x) | (mt__spread_2d_32(y) << 1);
}

MTDEF void mt_decode_2d(uint32_t code, uint16_t* x, uint16_t* y) {
    *x = mt__compact_2d_32(code);
    *y = mt__compact_2d_32(code >> 1);
}

MTDEF uint64_t mt_encode_2d_64(uint32_t x, uint32_t y) {
    return mt__spread_2d_64(x) | (mt__spread_2d_64(y) << 1);
}

MTDEF void mt_decode_2d_64(uint64_t code, uint32_t* x, uint32_t* y) {
    *x = mt__compact_2d_64(code);
    *y = mt__compact_2d_64(code >> 1);
}

// ──3D Encode/Decode -------------------------------------------------------

MTDEF uint32_t mt_encode_3d(uint16_t x, uint16_t y, uint16_t z) {
    return mt__spread_3d_32(x) | (mt__spread_3d_32(y) << 1) | (mt__spread_3d_32(z) << 2);
}

MTDEF void mt_decode_3d(uint32_t code, uint16_t* x, uint16_t* y, uint16_t* z) {
    *x = mt__compact_3d_32(code);
    *y = mt__compact_3d_32(code >> 1);
    *z = mt__compact_3d_32(code >> 2);
}

MTDEF uint64_t mt_encode_3d_64(uint32_t x, uint32_t y, uint32_t z) {
    return mt__spread_3d_64(x) | (mt__spread_3d_64(y) << 1) | (mt__spread_3d_64(z) << 2);
}

MTDEF void mt_decode_3d_64(uint64_t code, uint32_t* x, uint32_t* y, uint32_t* z) {
    *x = mt__compact_3d_64(code);
    *y = mt__compact_3d_64(code >> 1);
    *z = mt__compact_3d_64(code >> 2);
}

// ──Iterators --------------------------------------------------------------

MTDEF mt_iter2d mt_iter2d_start(uint16_t nx, uint16_t ny) {
    mt_iter2d it = {0};
    it.nx = nx;
    it.ny = ny;
    it.remaining = (uint32_t)nx * (uint32_t)ny;
    it.code = 0;
    it.x = 0;
    it.y = 0;
    // If (0,0) is out of bounds (nx==0 or ny==0), remaining is already 0.
    return it;
}

MTDEF bool mt_iter2d_valid(const mt_iter2d* it) {
    return it->remaining > 0;
}

MTDEF void mt_iter2d_next(mt_iter2d* it) {
    if (it->remaining == 0) return;
    it->remaining--;
    if (it->remaining == 0) return;
    // Advance to next valid Morton code within grid bounds.
    // We need to find the smallest power-of-2 bounding box.
    for (;;) {
        it->code++;
        uint16_t x, y;
        mt_decode_2d(it->code, &x, &y);
        if (x < it->nx && y < it->ny) {
            it->x = x;
            it->y = y;
            return;
        }
    }
}

MTDEF mt_iter3d mt_iter3d_start(uint16_t nx, uint16_t ny, uint16_t nz) {
    mt_iter3d it = {0};
    it.nx = nx;
    it.ny = ny;
    it.nz = nz;
    it.remaining = (uint32_t)nx * (uint32_t)ny * (uint32_t)nz;
    it.code = 0;
    it.x = 0;
    it.y = 0;
    it.z = 0;
    return it;
}

MTDEF bool mt_iter3d_valid(const mt_iter3d* it) {
    return it->remaining > 0;
}

MTDEF void mt_iter3d_next(mt_iter3d* it) {
    if (it->remaining == 0) return;
    it->remaining--;
    if (it->remaining == 0) return;
    for (;;) {
        it->code++;
        uint16_t x, y, z;
        mt_decode_3d(it->code, &x, &y, &z);
        if (x < it->nx && y < it->ny && z < it->nz) {
            it->x = x;
            it->y = y;
            it->z = z;
            return;
        }
    }
}

// ──Utility ─────────────────────────────────────────────────────────────────

MTDEF int mt_compare_3d(uint16_t x1, uint16_t y1, uint16_t z1,
                        uint16_t x2, uint16_t y2, uint16_t z2) {
    uint32_t a = mt_encode_3d(x1, y1, z1);
    uint32_t b = mt_encode_3d(x2, y2, z2);
    if (a < b) return -1;
    if (a > b) return  1;
    return 0;
}

MTDEF const char* mt_version_str(void) {
    return "0.1.0";
}

#endif // MT_IMPLEMENTATION
#endif // MORTON_H
