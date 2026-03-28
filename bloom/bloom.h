// bloom.h — Pure C23 Bloom filter library
// Single-header: declare API, then #define BL_IMPLEMENTATION in one .c file.
// Fast negative cache lookups and deduplication checks.
#ifndef BLOOM_H
#define BLOOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ── Version ─────────────────────────────────────────────────────────────────

#define BL_VERSION_MAJOR 0
#define BL_VERSION_MINOR 1
#define BL_VERSION_PATCH 0

// ── C23 Compat ──────────────────────────────────────────────────────────────

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define BL_NODISCARD    [[nodiscard]]
  #define BL_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define BL_NODISCARD    __attribute__((warn_unused_result))
  #define BL_MAYBE_UNUSED __attribute__((unused))
#else
  #define BL_NODISCARD
  #define BL_MAYBE_UNUSED
#endif

// ── Linkage ─────────────────────────────────────────────────────────────────

#ifndef BLDEF
  #ifdef BL_STATIC
    #define BLDEF static
  #else
    #define BLDEF extern
  #endif
#endif

// ── Allocator Hooks ─────────────────────────────────────────────────────────

#ifndef BL_MALLOC
  #include <stdlib.h>
  #define BL_MALLOC(sz)       malloc(sz)
  #define BL_FREE(p)          free(p)
  #define BL_CALLOC(n, sz)    calloc(n, sz)
#endif

// ── Status Codes ────────────────────────────────────────────────────────────

typedef enum bl_status {
    BL_OK = 0,
    BL_ERR_NULL_ARG,
    BL_ERR_ALLOC,
    BL_ERR_INVALID_ARG,   // bad expected_items, fpr, etc.
    BL_ERR_IO,            // file I/O error
    BL_ERR_NOT_COUNTING,  // remove called on non-counting filter
    BL_ERR_OVERFLOW,      // counter overflow in counting filter
} bl_status;

// ── Types ───────────────────────────────────────────────────────────────────

typedef struct bl_filter bl_filter;

// ── Lifecycle ───────────────────────────────────────────────────────────────

BL_NODISCARD BLDEF bl_status bl_create(bl_filter** out, int64_t expected_items,
                                       double false_positive_rate);

BL_NODISCARD BLDEF bl_status bl_counting_create(bl_filter** out,
                                                int64_t expected_items,
                                                double false_positive_rate);

BLDEF void bl_free(bl_filter* f);

BL_NODISCARD BLDEF bl_status bl_clear(bl_filter* f);

// ── Operations ──────────────────────────────────────────────────────────────

BL_NODISCARD BLDEF bl_status bl_add(bl_filter* f, const void* data,
                                    int64_t len);
BL_NODISCARD BLDEF bl_status bl_add_u64(bl_filter* f, uint64_t key);
BL_NODISCARD BLDEF bl_status bl_add_str(bl_filter* f, const char* str);

BLDEF bool bl_test(const bl_filter* f, const void* data, int64_t len);
BLDEF bool bl_test_u64(const bl_filter* f, uint64_t key);
BLDEF bool bl_test_str(const bl_filter* f, const char* str);

// ── Counting Filter ────────────────────────────────────────────────────────

BL_NODISCARD BLDEF bl_status bl_remove(bl_filter* f, const void* data,
                                       int64_t len);

// ── Stats ───────────────────────────────────────────────────────────────────

BLDEF int64_t bl_count(const bl_filter* f);
BLDEF double  bl_fpr(const bl_filter* f);
BLDEF int64_t bl_size_bytes(const bl_filter* f);
BLDEF int32_t bl_num_hashes(const bl_filter* f);

// ── Serialization ───────────────────────────────────────────────────────────

BL_NODISCARD BLDEF bl_status bl_save(const bl_filter* f, const char* path);
BL_NODISCARD BLDEF bl_status bl_load(bl_filter** out, const char* path);

// ── Utilities ───────────────────────────────────────────────────────────────

BLDEF const char* bl_status_str(bl_status s);
BLDEF const char* bl_version_str(void);

// ═══════════════════════════════════════════════════════════════════════════
// ██  IMPLEMENTATION  ██
// ═══════════════════════════════════════════════════════════════════════════

#ifdef BL_IMPLEMENTATION

#include <math.h>

// ── Internal: Filter Structure ──────────────────────────────────────────────

struct bl_filter {
    uint64_t* bits;          // bit array (or packed 4-bit counters)
    int64_t   num_bits;      // total number of bits (m)
    int64_t   num_words;     // number of uint64_t words
    int32_t   num_hashes_k;  // number of hash functions (k)
    int64_t   items_added;   // count of items added
    bool      counting;      // true if counting filter
};

// ── Internal: Magic for serialization ───────────────────────────────────────

#define BL_MAGIC 0x424C4F4F4D464C54ULL  // "BLOOMFLT"

// ── Internal: FNV-1a 64-bit Hash ────────────────────────────────────────────

static uint64_t bl__fnv1a(const void* data, int64_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = 14695981039346656037ULL;
    for (int64_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// Second independent hash using a different seed/mix.
static uint64_t bl__hash2(const void* data, int64_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = 0xcbf29ce484222325ULL ^ 0x6c62272e07bb0142ULL;
    for (int64_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    // Extra mixing
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

// ── Internal: Optimal Sizing ────────────────────────────────────────────────

static int64_t bl__optimal_num_bits(int64_t n, double p) {
    // m = -n * ln(p) / (ln2)^2
    double m = -((double)n * log(p)) / (log(2.0) * log(2.0));
    int64_t bits = (int64_t)ceil(m);
    if (bits < 64) bits = 64;
    return bits;
}

static int32_t bl__optimal_num_hashes(int64_t m, int64_t n) {
    // k = (m/n) * ln2
    double k = ((double)m / (double)n) * log(2.0);
    int32_t hashes = (int32_t)round(k);
    if (hashes < 1) hashes = 1;
    return hashes;
}

// ── Internal: Bit Operations ────────────────────────────────────────────────

static void bl__set_bit(uint64_t* bits, int64_t idx) {
    bits[idx >> 6] |= (1ULL << (idx & 63));
}

static bool bl__get_bit(const uint64_t* bits, int64_t idx) {
    return (bits[idx >> 6] & (1ULL << (idx & 63))) != 0;
}

// ── Internal: 4-bit Counter Operations ──────────────────────────────────────

static uint8_t bl__get_counter(const uint64_t* bits, int64_t idx) {
    int64_t word = idx >> 4;       // 16 counters per uint64_t
    int shift = (int)((idx & 15) * 4);
    return (uint8_t)((bits[word] >> shift) & 0xFULL);
}

static void bl__set_counter(uint64_t* bits, int64_t idx, uint8_t val) {
    int64_t word = idx >> 4;
    int shift = (int)((idx & 15) * 4);
    bits[word] &= ~(0xFULL << shift);
    bits[word] |= ((uint64_t)(val & 0xF) << shift);
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

BL_NODISCARD BLDEF bl_status bl_create(bl_filter** out, int64_t expected_items,
                                       double false_positive_rate) {
    if (!out) return BL_ERR_NULL_ARG;
    if (expected_items <= 0 || false_positive_rate <= 0.0 ||
        false_positive_rate >= 1.0)
        return BL_ERR_INVALID_ARG;

    bl_filter* f = (bl_filter*)BL_MALLOC(sizeof(bl_filter));
    if (!f) return BL_ERR_ALLOC;

    f->num_bits = bl__optimal_num_bits(expected_items, false_positive_rate);
    f->num_hashes_k = bl__optimal_num_hashes(f->num_bits, expected_items);
    f->num_words = (f->num_bits + 63) / 64;
    f->items_added = 0;
    f->counting = false;

    f->bits = (uint64_t*)BL_CALLOC((size_t)f->num_words, sizeof(uint64_t));
    if (!f->bits) {
        BL_FREE(f);
        return BL_ERR_ALLOC;
    }

    *out = f;
    return BL_OK;
}

BL_NODISCARD BLDEF bl_status bl_counting_create(bl_filter** out,
                                                int64_t expected_items,
                                                double false_positive_rate) {
    if (!out) return BL_ERR_NULL_ARG;
    if (expected_items <= 0 || false_positive_rate <= 0.0 ||
        false_positive_rate >= 1.0)
        return BL_ERR_INVALID_ARG;

    bl_filter* f = (bl_filter*)BL_MALLOC(sizeof(bl_filter));
    if (!f) return BL_ERR_ALLOC;

    f->num_bits = bl__optimal_num_bits(expected_items, false_positive_rate);
    f->num_hashes_k = bl__optimal_num_hashes(f->num_bits, expected_items);
    f->items_added = 0;
    f->counting = true;

    // For counting filter, we need 4 bits per position.
    // Pack 16 counters per uint64_t.
    f->num_words = (f->num_bits + 15) / 16;

    f->bits = (uint64_t*)BL_CALLOC((size_t)f->num_words, sizeof(uint64_t));
    if (!f->bits) {
        BL_FREE(f);
        return BL_ERR_ALLOC;
    }

    *out = f;
    return BL_OK;
}

BLDEF void bl_free(bl_filter* f) {
    if (!f) return;
    BL_FREE(f->bits);
    BL_FREE(f);
}

BL_NODISCARD BLDEF bl_status bl_clear(bl_filter* f) {
    if (!f) return BL_ERR_NULL_ARG;
    memset(f->bits, 0, (size_t)f->num_words * sizeof(uint64_t));
    f->items_added = 0;
    return BL_OK;
}

// ── Operations ──────────────────────────────────────────────────────────────

BL_NODISCARD BLDEF bl_status bl_add(bl_filter* f, const void* data,
                                    int64_t len) {
    if (!f || !data) return BL_ERR_NULL_ARG;

    uint64_t h1 = bl__fnv1a(data, len);
    uint64_t h2 = bl__hash2(data, len);

    for (int32_t i = 0; i < f->num_hashes_k; i++) {
        uint64_t combined = h1 + (uint64_t)i * h2;
        int64_t idx = (int64_t)(combined % (uint64_t)f->num_bits);
        if (f->counting) {
            uint8_t val = bl__get_counter(f->bits, idx);
            if (val < 15) {
                bl__set_counter(f->bits, idx, val + 1);
            }
            // Saturate at 15; silently cap.
        } else {
            bl__set_bit(f->bits, idx);
        }
    }

    f->items_added++;
    return BL_OK;
}

BL_NODISCARD BLDEF bl_status bl_add_u64(bl_filter* f, uint64_t key) {
    return bl_add(f, &key, (int64_t)sizeof(key));
}

BL_NODISCARD BLDEF bl_status bl_add_str(bl_filter* f, const char* str) {
    if (!str) return BL_ERR_NULL_ARG;
    return bl_add(f, str, (int64_t)strlen(str));
}

BLDEF bool bl_test(const bl_filter* f, const void* data, int64_t len) {
    if (!f || !data) return false;

    uint64_t h1 = bl__fnv1a(data, len);
    uint64_t h2 = bl__hash2(data, len);

    for (int32_t i = 0; i < f->num_hashes_k; i++) {
        uint64_t combined = h1 + (uint64_t)i * h2;
        int64_t idx = (int64_t)(combined % (uint64_t)f->num_bits);
        if (f->counting) {
            if (bl__get_counter(f->bits, idx) == 0) return false;
        } else {
            if (!bl__get_bit(f->bits, idx)) return false;
        }
    }

    return true;
}

BLDEF bool bl_test_u64(const bl_filter* f, uint64_t key) {
    return bl_test(f, &key, (int64_t)sizeof(key));
}

BLDEF bool bl_test_str(const bl_filter* f, const char* str) {
    if (!str) return false;
    return bl_test(f, str, (int64_t)strlen(str));
}

// ── Counting Filter: Remove ────────────────────────────────────────────────

BL_NODISCARD BLDEF bl_status bl_remove(bl_filter* f, const void* data,
                                       int64_t len) {
    if (!f || !data) return BL_ERR_NULL_ARG;
    if (!f->counting) return BL_ERR_NOT_COUNTING;

    uint64_t h1 = bl__fnv1a(data, len);
    uint64_t h2 = bl__hash2(data, len);

    // First verify all counters are > 0 (item is present).
    for (int32_t i = 0; i < f->num_hashes_k; i++) {
        uint64_t combined = h1 + (uint64_t)i * h2;
        int64_t idx = (int64_t)(combined % (uint64_t)f->num_bits);
        if (bl__get_counter(f->bits, idx) == 0) return BL_OK;  // not present
    }

    // Decrement all counters.
    for (int32_t i = 0; i < f->num_hashes_k; i++) {
        uint64_t combined = h1 + (uint64_t)i * h2;
        int64_t idx = (int64_t)(combined % (uint64_t)f->num_bits);
        uint8_t val = bl__get_counter(f->bits, idx);
        if (val > 0) {
            bl__set_counter(f->bits, idx, val - 1);
        }
    }

    if (f->items_added > 0) f->items_added--;
    return BL_OK;
}

// ── Stats ───────────────────────────────────────────────────────────────────

BLDEF int64_t bl_count(const bl_filter* f) {
    if (!f) return 0;
    return f->items_added;
}

BLDEF double bl_fpr(const bl_filter* f) {
    if (!f) return 1.0;
    // Estimated FPR = (1 - e^(-k*n/m))^k
    double k = (double)f->num_hashes_k;
    double n = (double)f->items_added;
    double m = (double)f->num_bits;
    return pow(1.0 - exp(-k * n / m), k);
}

BLDEF int64_t bl_size_bytes(const bl_filter* f) {
    if (!f) return 0;
    return (int64_t)(sizeof(bl_filter) +
                     (size_t)f->num_words * sizeof(uint64_t));
}

BLDEF int32_t bl_num_hashes(const bl_filter* f) {
    if (!f) return 0;
    return f->num_hashes_k;
}

// ── Serialization ───────────────────────────────────────────────────────────

BL_NODISCARD BLDEF bl_status bl_save(const bl_filter* f, const char* path) {
    if (!f || !path) return BL_ERR_NULL_ARG;

    FILE* fp = fopen(path, "wb");
    if (!fp) return BL_ERR_IO;

    uint64_t magic = BL_MAGIC;
    if (fwrite(&magic, sizeof(magic), 1, fp) != 1) goto fail;
    if (fwrite(&f->num_bits, sizeof(f->num_bits), 1, fp) != 1) goto fail;
    if (fwrite(&f->num_words, sizeof(f->num_words), 1, fp) != 1) goto fail;
    if (fwrite(&f->num_hashes_k, sizeof(f->num_hashes_k), 1, fp) != 1)
        goto fail;
    if (fwrite(&f->items_added, sizeof(f->items_added), 1, fp) != 1)
        goto fail;
    uint8_t cnt = f->counting ? 1 : 0;
    if (fwrite(&cnt, sizeof(cnt), 1, fp) != 1) goto fail;
    if (fwrite(f->bits, sizeof(uint64_t), (size_t)f->num_words, fp) !=
        (size_t)f->num_words)
        goto fail;

    fclose(fp);
    return BL_OK;

fail:
    fclose(fp);
    return BL_ERR_IO;
}

BL_NODISCARD BLDEF bl_status bl_load(bl_filter** out, const char* path) {
    if (!out || !path) return BL_ERR_NULL_ARG;

    FILE* fp = fopen(path, "rb");
    if (!fp) return BL_ERR_IO;

    uint64_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != BL_MAGIC)
        goto fail;

    bl_filter* f = (bl_filter*)BL_MALLOC(sizeof(bl_filter));
    if (!f) { fclose(fp); return BL_ERR_ALLOC; }

    if (fread(&f->num_bits, sizeof(f->num_bits), 1, fp) != 1) goto fail2;
    if (fread(&f->num_words, sizeof(f->num_words), 1, fp) != 1) goto fail2;
    if (fread(&f->num_hashes_k, sizeof(f->num_hashes_k), 1, fp) != 1)
        goto fail2;
    if (fread(&f->items_added, sizeof(f->items_added), 1, fp) != 1)
        goto fail2;
    uint8_t cnt = 0;
    if (fread(&cnt, sizeof(cnt), 1, fp) != 1) goto fail2;
    f->counting = cnt != 0;

    f->bits = (uint64_t*)BL_MALLOC((size_t)f->num_words * sizeof(uint64_t));
    if (!f->bits) goto fail2;

    if (fread(f->bits, sizeof(uint64_t), (size_t)f->num_words, fp) !=
        (size_t)f->num_words) {
        BL_FREE(f->bits);
        goto fail2;
    }

    fclose(fp);
    *out = f;
    return BL_OK;

fail2:
    BL_FREE(f);
fail:
    fclose(fp);
    return BL_ERR_IO;
}

// ── Utilities ───────────────────────────────────────────────────────────────

BLDEF const char* bl_status_str(bl_status s) {
    switch (s) {
        case BL_OK:             return "BL_OK";
        case BL_ERR_NULL_ARG:   return "BL_ERR_NULL_ARG";
        case BL_ERR_ALLOC:      return "BL_ERR_ALLOC";
        case BL_ERR_INVALID_ARG:return "BL_ERR_INVALID_ARG";
        case BL_ERR_IO:         return "BL_ERR_IO";
        case BL_ERR_NOT_COUNTING:return "BL_ERR_NOT_COUNTING";
        case BL_ERR_OVERFLOW:   return "BL_ERR_OVERFLOW";
    }
    return "BL_UNKNOWN";
}

BLDEF const char* bl_version_str(void) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d",
             BL_VERSION_MAJOR, BL_VERSION_MINOR, BL_VERSION_PATCH);
    return buf;
}

#endif // BL_IMPLEMENTATION
#endif // BLOOM_H
