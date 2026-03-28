/*
 * cx_base64.h - Base64 encoding and decoding.
 * Part of libcx. Pure C89.
 */

#ifndef CX_BASE64_H
#define CX_BASE64_H

#include <stddef.h>

/* Encode inlen bytes from in into out. outlen must point to the size of out.
 * On return, *outlen is set to the number of bytes written.
 * Returns 0 on success, -1 if out is too small. */
int cx_base64_encode(const void *in, size_t inlen, char *out, size_t *outlen);

/* Decode inlen bytes of base64 from in into out. outlen must point to the size of out.
 * On return, *outlen is set to the number of bytes written.
 * Returns 0 on success, -1 on error. */
int cx_base64_decode(const char *in, size_t inlen, void *out, size_t *outlen);

/* Return the output buffer size needed for encoding inlen bytes. */
size_t cx_base64_encode_size(size_t inlen);

/* Return the max output buffer size needed for decoding inlen base64 chars. */
size_t cx_base64_decode_size(size_t inlen);

#endif
