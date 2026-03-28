/* Kernel pattern: static assertions and compile-time checks */
#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/kernel.h>

struct packed_header {
    u8 version;
    u8 type;
    u16 length;
    u32 id;
    u64 timestamp;
} __attribute__((packed));

struct aligned_data {
    u64 value;
    u32 flags;
    u16 tag;
    u8 padding[2];
} __attribute__((aligned(16)));

/* Compile-time size checks */
_Static_assert(sizeof(struct packed_header) == 16,
               "packed_header must be 16 bytes");
_Static_assert(sizeof(u8) == 1, "u8 must be 1 byte");
_Static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
_Static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
_Static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");

/* Verify alignment */
_Static_assert(sizeof(struct aligned_data) == 16,
               "aligned_data must be 16 bytes");

/* Power of 2 check */
#define IS_POWER_OF_2(n) (((n) != 0) && (((n) & ((n) - 1)) == 0))

enum {
    PAGE_SIZE_VAL = 4096,
    CACHE_LINE_SIZE = 64,
};

_Static_assert(IS_POWER_OF_2(PAGE_SIZE_VAL), "PAGE_SIZE must be power of 2");
_Static_assert(IS_POWER_OF_2(CACHE_LINE_SIZE), "CACHE_LINE must be power of 2");

struct bit_fields {
    unsigned int a : 3;
    unsigned int b : 5;
    unsigned int c : 8;
    unsigned int d : 16;
};

_Static_assert(sizeof(struct bit_fields) == 4, "bitfields must fit in 4 bytes");

void test_static_assert(void)
{
    struct packed_header hdr;
    struct aligned_data data;
    struct bit_fields bf;

    hdr.version = 1;
    hdr.type = 2;
    hdr.length = 100;
    hdr.id = 42;
    hdr.timestamp = 1234567890ULL;

    data.value = 0xDEADBEEF;
    data.flags = 0xFF;
    data.tag = 1;

    bf.a = 7;
    bf.b = 31;
    bf.c = 255;
    bf.d = 65535;

    (void)hdr; (void)data; (void)bf;
}
