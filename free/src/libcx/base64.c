/*
 * base64.c - Base64 encoding and decoding.
 * Part of libcx. Pure C89.
 */

#include "cx_base64.h"
#include <string.h>

static const char b64_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const unsigned char b64_dec[256] = {
    /* 0x00-0x0F */ 255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    /* 0x10-0x1F */ 255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    /* 0x20-0x2F */ 255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
    /* 0x30-0x3F */  52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
    /* 0x40-0x4F */ 255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    /* 0x50-0x5F */  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
    /* 0x60-0x6F */ 255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    /* 0x70-0x7F */  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
    /* 0x80-0xFF */ 255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
                   255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
                   255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
                   255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
                   255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
                   255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
                   255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
                   255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};

size_t cx_base64_encode_size(size_t inlen)
{
    return ((inlen + 2) / 3) * 4 + 1;
}

size_t cx_base64_decode_size(size_t inlen)
{
    return (inlen / 4) * 3 + 1;
}

int cx_base64_encode(const void *in, size_t inlen, char *out, size_t *outlen)
{
    const unsigned char *src = (const unsigned char *)in;
    size_t needed;
    size_t i;
    size_t j;
    unsigned long triple;

    needed = ((inlen + 2) / 3) * 4;
    if (*outlen < needed + 1) {
        return -1;
    }

    j = 0;
    for (i = 0; i + 2 < inlen; i += 3) {
        triple = ((unsigned long)src[i] << 16) |
                 ((unsigned long)src[i + 1] << 8) |
                 ((unsigned long)src[i + 2]);
        out[j++] = b64_enc[(triple >> 18) & 0x3F];
        out[j++] = b64_enc[(triple >> 12) & 0x3F];
        out[j++] = b64_enc[(triple >> 6)  & 0x3F];
        out[j++] = b64_enc[triple & 0x3F];
    }

    if (i < inlen) {
        triple = (unsigned long)src[i] << 16;
        if (i + 1 < inlen) {
            triple |= (unsigned long)src[i + 1] << 8;
        }
        out[j++] = b64_enc[(triple >> 18) & 0x3F];
        out[j++] = b64_enc[(triple >> 12) & 0x3F];
        out[j++] = (i + 1 < inlen) ? b64_enc[(triple >> 6) & 0x3F] : '=';
        out[j++] = '=';
    }

    out[j] = '\0';
    *outlen = j;
    return 0;
}

int cx_base64_decode(const char *in, size_t inlen, void *out, size_t *outlen)
{
    unsigned char *dst = (unsigned char *)out;
    size_t i;
    size_t j;
    unsigned long sextet[4];
    unsigned long triple;
    size_t max_out;

    /* Skip trailing whitespace/newlines, find real length */
    while (inlen > 0 && (in[inlen - 1] == '\n' || in[inlen - 1] == '\r' ||
                          in[inlen - 1] == ' ')) {
        inlen--;
    }

    if (inlen % 4 != 0) return -1;

    max_out = (inlen / 4) * 3;
    if (inlen > 0 && in[inlen - 1] == '=') max_out--;
    if (inlen > 1 && in[inlen - 2] == '=') max_out--;

    if (*outlen < max_out) return -1;

    j = 0;
    for (i = 0; i < inlen; i += 4) {
        int k;
        for (k = 0; k < 4; k++) {
            if (in[i + k] == '=') {
                sextet[k] = 0;
            } else {
                sextet[k] = b64_dec[(unsigned char)in[i + k]];
                if (sextet[k] == 255) return -1;
            }
        }

        triple = (sextet[0] << 18) | (sextet[1] << 12) |
                 (sextet[2] << 6)  | sextet[3];

        if (j < max_out) dst[j++] = (unsigned char)((triple >> 16) & 0xFF);
        if (j < max_out) dst[j++] = (unsigned char)((triple >> 8)  & 0xFF);
        if (j < max_out) dst[j++] = (unsigned char)(triple & 0xFF);
    }

    *outlen = j;
    return 0;
}
