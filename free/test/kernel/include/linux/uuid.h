/* SPDX-License-Identifier: GPL-2.0 */
/* Stub uuid.h for free-cc kernel compilation testing */
#ifndef _LINUX_UUID_H
#define _LINUX_UUID_H

#include <linux/types.h>

typedef struct {
    u8 b[16];
} guid_t;

typedef struct {
    u8 b[16];
} uuid_t;

#define GUID_INIT(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
    ((guid_t){{ (a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, \
    ((a) >> 24) & 0xff, (b) & 0xff, ((b) >> 8) & 0xff, \
    (c) & 0xff, ((c) >> 8) & 0xff, \
    (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }})

#define UUID_INIT(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
    ((uuid_t){{ ((a) >> 24) & 0xff, ((a) >> 16) & 0xff, \
    ((a) >> 8) & 0xff, (a) & 0xff, \
    ((b) >> 8) & 0xff, (b) & 0xff, \
    ((c) >> 8) & 0xff, (c) & 0xff, \
    (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }})

#define UUID_STRING_LEN 36

extern const guid_t guid_null;
extern const uuid_t uuid_null;

static inline bool guid_equal(const guid_t *u1, const guid_t *u2)
{
    return __builtin_memcmp(u1, u2, sizeof(guid_t)) == 0;
}

static inline bool uuid_equal(const uuid_t *u1, const uuid_t *u2)
{
    return __builtin_memcmp(u1, u2, sizeof(uuid_t)) == 0;
}

static inline bool guid_is_null(const guid_t *guid)
{
    return guid_equal(guid, &guid_null);
}

static inline bool uuid_is_null(const uuid_t *uuid)
{
    return uuid_equal(uuid, &uuid_null);
}

int guid_parse(const char *uuid, guid_t *u);
int uuid_parse(const char *uuid, uuid_t *u);

extern void generate_random_uuid(unsigned char uuid[16]);
extern void generate_random_guid(unsigned char guid[16]);

#endif /* _LINUX_UUID_H */
