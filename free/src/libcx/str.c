/*
 * str.c - Dynamic string builder implementation.
 * Part of libcx. Pure C89.
 */

#include "cx_str.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define CX_STR_INIT_CAP 32

static void cx_str_grow(cx_str *s, int needed)
{
    int newcap;
    char *newdata;

    newcap = s->cap == 0 ? CX_STR_INIT_CAP : s->cap;
    while (newcap < needed) {
        newcap *= 2;
    }

    newdata = (char *)malloc((size_t)newcap);
    if (s->data) {
        memcpy(newdata, s->data, (size_t)s->len);
        free(s->data);
    }
    s->data = newdata;
    s->cap = newcap;
}

cx_str cx_str_new(void)
{
    cx_str s;
    s.data = NULL;
    s.len = 0;
    s.cap = 0;
    return s;
}

void cx_str_append(cx_str *s, const char *data, int len)
{
    if (s->len + len + 1 > s->cap) {
        cx_str_grow(s, s->len + len + 1);
    }
    memcpy(s->data + s->len, data, (size_t)len);
    s->len += len;
    s->data[s->len] = '\0';
}

void cx_str_appendf(cx_str *s, const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    int len;

    va_start(ap, fmt);
    len = vsprintf(buf, fmt, ap);
    va_end(ap);

    if (len > 0) {
        cx_str_append(s, buf, len);
    }
}

void cx_str_appendc(cx_str *s, char c)
{
    if (s->len + 2 > s->cap) {
        cx_str_grow(s, s->len + 2);
    }
    s->data[s->len++] = c;
    s->data[s->len] = '\0';
}

char *cx_str_cstr(cx_str *s)
{
    if (!s->data) {
        cx_str_grow(s, 1);
        s->data[0] = '\0';
    }
    return s->data;
}

int cx_str_len(cx_str *s)
{
    return s->len;
}

void cx_str_clear(cx_str *s)
{
    s->len = 0;
    if (s->data) {
        s->data[0] = '\0';
    }
}

void cx_str_free(cx_str *s)
{
    free(s->data);
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

cx_str cx_str_dup(cx_str *s)
{
    cx_str r;
    r = cx_str_new();
    if (s->len > 0) {
        cx_str_append(&r, s->data, s->len);
    }
    return r;
}

cx_str cx_str_slice(cx_str *s, int start, int end)
{
    cx_str r;
    r = cx_str_new();

    if (start < 0) start = 0;
    if (end > s->len) end = s->len;
    if (start >= end) return r;

    cx_str_append(&r, s->data + start, end - start);
    return r;
}

int cx_str_find(cx_str *s, const char *needle)
{
    char *p;
    if (!s->data) return -1;
    s->data[s->len] = '\0';
    p = strstr(s->data, needle);
    if (!p) return -1;
    return (int)(p - s->data);
}
