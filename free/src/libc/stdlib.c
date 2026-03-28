/*
 * stdlib.c - Standard library functions for the free libc.
 * Pure C89. No external dependencies.
 */

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>

/* syscall interface (from syscall.S) */
long __syscall(long num, long a1, long a2, long a3,
               long a4, long a5, long a6);

/* syscall numbers */
#define SYS_EXIT_GROUP 94
#define SYS_BRK        214

/* environ - set by _start or __libc_start_main */
char **environ = 0;

/* ------------------------------------------------------------------ */
/* Memory allocator: brk-based bump allocator with free list          */
/* ------------------------------------------------------------------ */

/* allocation header, placed just before returned pointer */
struct alloc_hdr {
    size_t size;              /* usable size (not including header) */
    struct alloc_hdr *next;   /* next free block, or NULL if in use */
};

#define HDR_SIZE     sizeof(struct alloc_hdr)
#define ALIGN16(x)   (((x) + 15) & ~(size_t)15)

static void *heap_start = NULL;
static void *heap_end   = NULL;
static struct alloc_hdr *free_list = NULL;

static void *sbrk_grow(size_t incr)
{
    long cur;
    long req;

    if (heap_start == NULL) {
        cur = __syscall(SYS_BRK, 0, 0, 0, 0, 0, 0);
        heap_start = (void *)cur;
        heap_end = (void *)cur;
    }

    cur = (long)heap_end;
    req = __syscall(SYS_BRK, cur + (long)incr, 0, 0, 0, 0, 0);
    if (req <= cur) {
        return NULL;
    }
    heap_end = (void *)req;
    return (void *)cur;
}

void *malloc(size_t size)
{
    size_t need;
    struct alloc_hdr *prev;
    struct alloc_hdr *cur;
    struct alloc_hdr *hdr;
    void *block;

    if (size == 0) {
        return NULL;
    }

    need = ALIGN16(size);

    /* search free list for a block that fits */
    prev = NULL;
    cur = free_list;
    while (cur != NULL) {
        if (cur->size >= need) {
            /* unlink from free list */
            if (prev != NULL) {
                prev->next = cur->next;
            } else {
                free_list = cur->next;
            }
            cur->next = NULL;
            return (void *)((char *)cur + HDR_SIZE);
        }
        prev = cur;
        cur = cur->next;
    }

    /* no free block found, grow heap */
    block = sbrk_grow(HDR_SIZE + need);
    if (block == NULL) {
        return NULL;
    }
    hdr = (struct alloc_hdr *)block;
    hdr->size = need;
    hdr->next = NULL;
    return (void *)((char *)hdr + HDR_SIZE);
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total;
    void *p;

    /* overflow check */
    if (nmemb != 0 && size > (size_t)-1 / nmemb) {
        return NULL;
    }
    total = nmemb * size;
    p = malloc(total);
    if (p != NULL) {
        memset(p, 0, total);
    }
    return p;
}

void *realloc(void *ptr, size_t size)
{
    struct alloc_hdr *hdr;
    void *newp;
    size_t copy_size;

    if (ptr == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    hdr = (struct alloc_hdr *)((char *)ptr - HDR_SIZE);
    if (hdr->size >= size) {
        return ptr;
    }

    newp = malloc(size);
    if (newp == NULL) {
        return NULL;
    }
    copy_size = hdr->size < size ? hdr->size : size;
    memcpy(newp, ptr, copy_size);
    free(ptr);
    return newp;
}

void free(void *ptr)
{
    struct alloc_hdr *hdr;

    if (ptr == NULL) {
        return;
    }
    hdr = (struct alloc_hdr *)((char *)ptr - HDR_SIZE);
    hdr->next = free_list;
    free_list = hdr;
}

/* ------------------------------------------------------------------ */
/* Process control                                                    */
/* ------------------------------------------------------------------ */

void exit(int status)
{
    __syscall(SYS_EXIT_GROUP, (long)status, 0, 0, 0, 0, 0);
    /* should not return, but satisfy compiler */
    for (;;) {}
}

void abort(void)
{
    exit(134);
}

/* ------------------------------------------------------------------ */
/* String-to-number conversions                                       */
/* ------------------------------------------------------------------ */

int atoi(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

long atol(const char *s)
{
    return strtol(s, NULL, 10);
}

static int digit_val(int ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 10;
    }
    return -1;
}

unsigned long strtoul(const char *s, char **endp, int base)
{
    unsigned long result = 0;
    int neg = 0;
    int d;

    /* skip whitespace */
    while (isspace((unsigned char)*s)) {
        s++;
    }

    /* optional sign */
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    /* auto-detect base */
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s += 2;
        }
    }

    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (endp != NULL) {
            *endp = (char *)s;
        }
        return 0;
    }

    while (*s != '\0') {
        d = digit_val((unsigned char)*s);
        if (d < 0 || d >= base) {
            break;
        }
        if (result > (ULONG_MAX - (unsigned long)d) / (unsigned long)base) {
            errno = ERANGE;
            result = ULONG_MAX;
            /* consume remaining valid digits */
            s++;
            while (*s != '\0') {
                d = digit_val((unsigned char)*s);
                if (d < 0 || d >= base) {
                    break;
                }
                s++;
            }
            break;
        }
        result = result * (unsigned long)base + (unsigned long)d;
        s++;
    }

    if (endp != NULL) {
        *endp = (char *)s;
    }
    return neg ? (unsigned long)(-(long)result) : result;
}

long strtol(const char *s, char **endp, int base)
{
    unsigned long val;
    int neg = 0;

    /* skip whitespace */
    while (isspace((unsigned char)*s)) {
        s++;
    }

    /* consume sign so strtoul does not double-handle it */
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    val = strtoul(s, endp, base);

    if (neg) {
        if (val > (unsigned long)LONG_MAX + 1UL) {
            errno = ERANGE;
            return LONG_MIN;
        }
        return -(long)val;
    } else {
        if (val > (unsigned long)LONG_MAX) {
            errno = ERANGE;
            return LONG_MAX;
        }
        return (long)val;
    }
}

/* ------------------------------------------------------------------ */
/* qsort - simple insertion sort (stable, good for small arrays)      */
/* ------------------------------------------------------------------ */

static void swap_bytes(char *a, char *b, size_t size)
{
    size_t i;
    char tmp;

    for (i = 0; i < size; i++) {
        tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

void qsort(void *base, size_t nmemb, size_t size,
            int (*cmp)(const void *, const void *))
{
    char *arr = (char *)base;
    size_t i;
    size_t j;

    if (nmemb < 2) {
        return;
    }

    for (i = 1; i < nmemb; i++) {
        j = i;
        while (j > 0 && cmp(arr + j * size, arr + (j - 1) * size) < 0) {
            swap_bytes(arr + j * size, arr + (j - 1) * size, size);
            j--;
        }
    }
}

/* ------------------------------------------------------------------ */
/* getenv - search the environ array                                  */
/* ------------------------------------------------------------------ */

char *getenv(const char *name)
{
    char **ep;
    const char *np;
    const char *cp;

    if (environ == NULL || name == NULL || *name == '\0') {
        return NULL;
    }

    for (ep = environ; *ep != NULL; ep++) {
        np = name;
        cp = *ep;
        while (*np != '\0' && *cp != '\0' && *np == *cp) {
            np++;
            cp++;
        }
        if (*np == '\0' && *cp == '=') {
            return (char *)(cp + 1);
        }
    }
    return NULL;
}
