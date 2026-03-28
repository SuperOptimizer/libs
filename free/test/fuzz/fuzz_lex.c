/*
 * fuzz_lex.c - Lexer fuzzer for the free C compiler.
 * Feeds arbitrary bytes to the C lexer; no input should cause a crash.
 * Reads from a file argument or stdin (for use with AFL/libfuzzer).
 *
 * Build (standalone):
 *   cc -std=c89 -I../../include -o fuzz_lex \
 *      fuzz_lex.c ../../src/cc/lex.c
 *
 * Pure C89. All variables at top of block.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include "free.h"

/* ---- arena implementation (self-contained for fuzzer) ---- */

void arena_init(struct arena *a, char *buf, usize cap)
{
    a->buf = buf;
    a->cap = cap;
    a->used = 0;
}

void *arena_alloc(struct arena *a, usize size)
{
    usize aligned;
    void *p;

    aligned = (size + 7) & ~(usize)7;
    if (a->used + aligned > a->cap) {
        /* out of arena space: return zeroed static block */
        static char fallback[4096];
        static usize fb_used = 0;
        if (fb_used + aligned > sizeof(fallback)) {
            fb_used = 0;
        }
        p = fallback + fb_used;
        fb_used += aligned;
        memset(p, 0, aligned);
        return p;
    }
    p = a->buf + a->used;
    a->used += aligned;
    memset(p, 0, aligned);
    return p;
}

void arena_reset(struct arena *a)
{
    a->used = 0;
}

/* ---- string utilities (needed by lexer) ---- */

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

/* ---- error handling with longjmp (no crash on errors) ---- */

static jmp_buf fuzz_jmp;
static int fuzz_had_error;

void err(const char *fmt, ...)
{
    (void)fmt;
    fuzz_had_error = 1;
    longjmp(fuzz_jmp, 1);
}

void err_at(const char *file, int line, int col, const char *fmt, ...)
{
    (void)file;
    (void)line;
    (void)col;
    (void)fmt;
    fuzz_had_error = 1;
    longjmp(fuzz_jmp, 1);
}

/* ---- lexer API declarations ---- */

void lex_init(const char *src, const char *filename, struct arena *a);
struct tok *lex_next(void);

/* ---- input reading ---- */

static char *read_input(FILE *f, long *out_len)
{
    char *buf;
    long len;
    long cap;
    int ch;

    cap = 4096;
    buf = (char *)malloc((size_t)cap);
    if (!buf) {
        return NULL;
    }
    len = 0;

    while ((ch = fgetc(f)) != EOF) {
        if (len + 2 >= cap) {
            cap *= 2;
            buf = (char *)realloc(buf, (size_t)cap);
            if (!buf) {
                return NULL;
            }
        }
        buf[len++] = (char)ch;
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    FILE *f;
    char *input;
    long input_len;
    char arena_buf[1024 * 256]; /* 256 KB arena */
    struct arena a;
    struct tok *t;

    /* open input: file argument or stdin */
    if (argc > 1) {
        f = fopen(argv[1], "rb");
        if (!f) {
            return 0; /* can't open file, not a crash */
        }
    } else {
        f = stdin;
    }

    input = read_input(f, &input_len);
    if (f != stdin) {
        fclose(f);
    }
    if (!input) {
        return 0; /* allocation failure, not a crash */
    }

    /* initialize arena */
    arena_init(&a, arena_buf, sizeof(arena_buf));

    /* set up error recovery */
    fuzz_had_error = 0;
    if (setjmp(fuzz_jmp) != 0) {
        /* error was caught, not a crash */
        free(input);
        return 0;
    }

    /* run lexer until EOF */
    lex_init(input, "<fuzz>", &a);
    for (;;) {
        t = lex_next();
        if (t->kind == TOK_EOF) {
            break;
        }
    }

    free(input);
    return 0;
}
