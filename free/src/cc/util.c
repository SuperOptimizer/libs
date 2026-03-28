/*
 * util.c - Arena allocator, string utilities, and error handling
 * for the free C compiler.
 * Pure C89. No external dependencies beyond system libc.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"

/* ---- arena allocator ---- */

void arena_init(struct arena *a, char *buf, usize cap)
{
    a->buf = buf;
    a->cap = cap;
    a->used = 0;
}

void *arena_alloc(struct arena *a, usize size)
{
    void *p;

    size = (size + 7) & ~(usize)7;
    if (a->used + size > a->cap) {
        fprintf(stderr, "arena_alloc: out of memory (%lu/%lu)\n",
                (unsigned long)(a->used + size),
                (unsigned long)a->cap);
        exit(1);
    }
    p = a->buf + a->used;
    a->used += size;
    return p;
}

void arena_reset(struct arena *a)
{
    a->used = 0;
}

/* ---- string utilities ---- */

int str_eq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

int str_eqn(const char *a, const char *b, int n)
{
    return strncmp(a, b, (size_t)n) == 0;
}

char *str_dup(struct arena *a, const char *s, int len)
{
    char *p;

    p = (char *)arena_alloc(a, (usize)(len + 1));
    memcpy(p, s, (size_t)len);
    p[len] = '\0';
    return p;
}

/* ---- error handling ---- */

void err(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "free-cc: error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

void err_at(const char *file, int line, int col, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: error: ", file, line, col);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}
