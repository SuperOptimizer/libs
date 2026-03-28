/*
 * utf8.c - UTF-8 encoding/decoding utilities.
 * Part of libcx. Pure C89.
 */

#include "cx_utf8.h"

int cx_utf8_decode(const char *s, int len, unsigned long *cp)
{
    unsigned char c;

    if (len <= 0 || !s) return 0;
    c = (unsigned char)s[0];

    if (c < 0x80) {
        *cp = c;
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        if (len < 2) return 0;
        if (((unsigned char)s[1] & 0xC0) != 0x80) return 0;
        *cp = ((unsigned long)(c & 0x1F) << 6) |
              ((unsigned long)((unsigned char)s[1] & 0x3F));
        if (*cp < 0x80) return 0; /* overlong */
        return 2;
    } else if ((c & 0xF0) == 0xE0) {
        if (len < 3) return 0;
        if (((unsigned char)s[1] & 0xC0) != 0x80) return 0;
        if (((unsigned char)s[2] & 0xC0) != 0x80) return 0;
        *cp = ((unsigned long)(c & 0x0F) << 12) |
              ((unsigned long)((unsigned char)s[1] & 0x3F) << 6) |
              ((unsigned long)((unsigned char)s[2] & 0x3F));
        if (*cp < 0x800) return 0; /* overlong */
        if (*cp >= 0xD800 && *cp <= 0xDFFF) return 0; /* surrogate */
        return 3;
    } else if ((c & 0xF8) == 0xF0) {
        if (len < 4) return 0;
        if (((unsigned char)s[1] & 0xC0) != 0x80) return 0;
        if (((unsigned char)s[2] & 0xC0) != 0x80) return 0;
        if (((unsigned char)s[3] & 0xC0) != 0x80) return 0;
        *cp = ((unsigned long)(c & 0x07) << 18) |
              ((unsigned long)((unsigned char)s[1] & 0x3F) << 12) |
              ((unsigned long)((unsigned char)s[2] & 0x3F) << 6) |
              ((unsigned long)((unsigned char)s[3] & 0x3F));
        if (*cp < 0x10000) return 0; /* overlong */
        if (*cp > 0x10FFFF) return 0; /* out of range */
        return 4;
    }

    return 0;
}

int cx_utf8_encode(unsigned long cp, char *buf)
{
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0; /* surrogate */
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

int cx_utf8_len(const char *s)
{
    int count = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) {
            s += 1;
        } else if ((c & 0xE0) == 0xC0) {
            s += 2;
        } else if ((c & 0xF0) == 0xE0) {
            s += 3;
        } else if ((c & 0xF8) == 0xF0) {
            s += 4;
        } else {
            s += 1; /* invalid, skip byte */
        }
        count++;
    }
    return count;
}

int cx_utf8_valid(const char *s, int len)
{
    const char *end = s + len;
    unsigned long cp;
    int n;

    while (s < end) {
        n = cx_utf8_decode(s, (int)(end - s), &cp);
        if (n == 0) return 0;
        s += n;
    }
    return 1;
}

const char *cx_utf8_next(const char *s)
{
    unsigned char c;
    if (!s || !*s) return NULL;
    c = (unsigned char)*s;

    if (c < 0x80) return s + 1;
    if ((c & 0xE0) == 0xC0) return s + 2;
    if ((c & 0xF0) == 0xE0) return s + 3;
    if ((c & 0xF8) == 0xF0) return s + 4;
    return s + 1; /* invalid byte, skip */
}
