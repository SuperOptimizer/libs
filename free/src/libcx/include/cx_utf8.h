/*
 * cx_utf8.h - UTF-8 encoding/decoding utilities.
 * Part of libcx. Pure C89.
 */

#ifndef CX_UTF8_H
#define CX_UTF8_H

#include <stddef.h>

/* Decode one codepoint from s (up to len bytes).
 * Returns number of bytes consumed, writes codepoint to *cp.
 * Returns 0 on error. */
int cx_utf8_decode(const char *s, int len, unsigned long *cp);

/* Encode a codepoint into buf (must have room for 4 bytes).
 * Returns number of bytes written, or 0 on error. */
int cx_utf8_encode(unsigned long cp, char *buf);

/* Count the number of codepoints in a NUL-terminated UTF-8 string. */
int cx_utf8_len(const char *s);

/* Validate that s[0..len-1] is valid UTF-8. Returns 1 if valid, 0 if not. */
int cx_utf8_valid(const char *s, int len);

/* Advance past one UTF-8 character. Returns pointer to next char, or NULL on error. */
const char *cx_utf8_next(const char *s);

#endif
