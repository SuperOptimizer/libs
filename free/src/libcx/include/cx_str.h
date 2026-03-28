/*
 * cx_str.h - Dynamic string builder.
 * Part of libcx. Pure C89.
 */

#ifndef CX_STR_H
#define CX_STR_H

#include <stddef.h>

typedef struct {
    char *data;
    int len;
    int cap;
} cx_str;

cx_str  cx_str_new(void);
void    cx_str_append(cx_str *s, const char *data, int len);
void    cx_str_appendf(cx_str *s, const char *fmt, ...);
void    cx_str_appendc(cx_str *s, char c);
char   *cx_str_cstr(cx_str *s);
int     cx_str_len(cx_str *s);
void    cx_str_clear(cx_str *s);
void    cx_str_free(cx_str *s);
cx_str  cx_str_dup(cx_str *s);
cx_str  cx_str_slice(cx_str *s, int start, int end);
int     cx_str_find(cx_str *s, const char *needle);

#endif
