/* Kernel pattern: UUID handling */
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/kernel.h>

struct resource_entry {
    guid_t guid;
    uuid_t uuid;
    const char *name;
    unsigned long flags;
};

static int guid_equal_check(const guid_t *a, const guid_t *b)
{
    return guid_equal(a, b);
}

static int uuid_is_null_check(const uuid_t *u)
{
    return uuid_is_null(u);
}

static void format_uuid(const uuid_t *u, char *out)
{
    const u8 *b = u->b;
    int i;
    /* Simple hex representation */
    for (i = 0; i < 16; i++) {
        static const char hex[] = "0123456789abcdef";
        out[i * 2] = hex[b[i] >> 4];
        out[i * 2 + 1] = hex[b[i] & 0xf];
    }
    out[32] = '\0';
}

void test_uuid(void)
{
    guid_t g1 = GUID_INIT(0x12345678, 0x1234, 0x5678,
                           0x12, 0x34, 0x56, 0x78,
                           0x9A, 0xBC, 0xDE, 0xF0);
    guid_t g2 = GUID_INIT(0x12345678, 0x1234, 0x5678,
                           0x12, 0x34, 0x56, 0x78,
                           0x9A, 0xBC, 0xDE, 0xF0);
    uuid_t u1;
    char buf[64];
    int eq;

    memset(&u1, 0, sizeof(u1));

    eq = guid_equal_check(&g1, &g2);
    (void)eq;

    (void)uuid_is_null_check(&u1);
    format_uuid(&u1, buf);
}
