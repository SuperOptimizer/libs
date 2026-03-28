/*
 * string.c - String and memory functions for the free libc.
 * Pure C89. No external dependencies.
 */

#include <string.h>
#include <stddef.h>
#include <stdlib.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;

    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;

    if (d < s) {
        for (i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        i = n;
        while (i > 0) {
            i--;
            d[i] = s[i];
        }
    }
    return dst;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    size_t i;

    for (i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    size_t i;

    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return a[i] - b[i];
        }
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    size_t i;

    for (i = 0; i < n; i++) {
        if (p[i] == uc) {
            return (void *)(p + i);
        }
    }
    return NULL;
}

size_t strlen(const char *s)
{
    size_t len = 0;

    while (s[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char *s1, const char *s2)
{
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;

    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    size_t i;

    if (n == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0') {
            return a[i] - b[i];
        }
    }
    return 0;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;

    while (*src != '\0') {
        *d++ = *src++;
    }
    *d = '\0';
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;

    for (i = 0; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = '\0';
    }
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;

    while (*d != '\0') {
        d++;
    }
    while (*src != '\0') {
        *d++ = *src++;
    }
    *d = '\0';
    return dst;
}

char *strchr(const char *s, int c)
{
    char ch = (char)c;

    while (*s != '\0') {
        if (*s == ch) {
            return (char *)s;
        }
        s++;
    }
    if (ch == '\0') {
        return (char *)s;
    }
    return NULL;
}

char *strrchr(const char *s, int c)
{
    char ch = (char)c;
    const char *last = NULL;

    while (*s != '\0') {
        if (*s == ch) {
            last = s;
        }
        s++;
    }
    if (ch == '\0') {
        return (char *)s;
    }
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t nlen;
    const char *h;

    if (*needle == '\0') {
        return (char *)haystack;
    }
    nlen = strlen(needle);
    for (h = haystack; *h != '\0'; h++) {
        if (strncmp(h, needle, nlen) == 0) {
            return (char *)h;
        }
    }
    return NULL;
}

char *strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);

    if (copy != NULL) {
        memcpy(copy, s, len);
    }
    return copy;
}
