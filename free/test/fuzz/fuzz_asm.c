/*
 * fuzz_asm.c - Assembler fuzzer for the free toolchain.
 * Feeds arbitrary bytes through the assembly lexer.
 * No input should cause a crash.
 * Reads from a file argument or stdin (for use with AFL/libfuzzer).
 *
 * Build (standalone):
 *   cc -std=c89 -I../../include -o fuzz_asm \
 *      fuzz_asm.c ../../src/as/lex.c ../../src/as/encode.c
 *
 * Pure C89. All variables at top of block.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"
#include "aarch64.h"

/* ---- assembly token types (mirror src/as/lex.c) ---- */

enum as_tok_kind {
    ASTOK_EOF = 0,
    ASTOK_NEWLINE,
    ASTOK_IDENT,
    ASTOK_LABEL,
    ASTOK_DIRECTIVE,
    ASTOK_REG,
    ASTOK_IMM,
    ASTOK_NUM,
    ASTOK_STRING,
    ASTOK_COMMA,
    ASTOK_LBRACKET,
    ASTOK_RBRACKET,
    ASTOK_EXCL,
    ASTOK_COLON,
    ASTOK_MINUS
};

struct as_token {
    enum as_tok_kind kind;
    const char *start;
    int len;
    long val;
    int is_wreg;
    int line;
};

struct as_lexer {
    const char *src;
    const char *pos;
    int line;
};

/* ---- assembly lexer API ---- */

void as_lex_init(struct as_lexer *lex, const char *src);
void as_next_token(struct as_lexer *lex, struct as_token *tok);

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
    struct as_lexer lex;
    struct as_token tok;

    /* open input */
    if (argc > 1) {
        f = fopen(argv[1], "rb");
        if (!f) {
            return 0;
        }
    } else {
        f = stdin;
    }

    input = read_input(f, &input_len);
    if (f != stdin) {
        fclose(f);
    }
    if (!input) {
        return 0;
    }

    /* run assembly lexer until EOF */
    as_lex_init(&lex, input);
    for (;;) {
        as_next_token(&lex, &tok);
        if (tok.kind == ASTOK_EOF) {
            break;
        }
    }

    /*
     * Exercise the instruction encoder with some values derived
     * from the token stream. The encoder functions are pure
     * computations (no memory access), so they should never crash
     * regardless of input values.
     */
    {
        int reg_a;
        int reg_b;
        int reg_c;
        u16 imm16;
        i32 offset;
        u32 imm12;
        u32 encoded;

        /* derive some values from the input bytes */
        reg_a = (input_len > 0) ? ((unsigned char)input[0] & 0x1f) : 0;
        reg_b = (input_len > 1) ? ((unsigned char)input[1] & 0x1f) : 0;
        reg_c = (input_len > 2) ? ((unsigned char)input[2] & 0x1f) : 0;
        imm16 = (input_len > 4)
            ? (u16)(((unsigned char)input[3] << 8) |
                    (unsigned char)input[4])
            : 0;
        offset = (input_len > 6)
            ? (i32)(((unsigned char)input[5] << 8) |
                    (unsigned char)input[6])
            : 0;
        imm12 = (input_len > 8)
            ? (u32)(((unsigned char)input[7] << 4) |
                    ((unsigned char)input[8] & 0xf))
            : 0;

        /* exercise all encoder functions */
        encoded = a64_movz(reg_a, imm16, 0);
        (void)encoded;
        encoded = a64_movk(reg_a, imm16, 16);
        (void)encoded;
        encoded = a64_movn(reg_a, imm16, 0);
        (void)encoded;
        encoded = a64_add_r(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_sub_r(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_subs_r(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_mul(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_sdiv(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_udiv(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_msub(reg_a, reg_b, reg_c, reg_a);
        (void)encoded;
        encoded = a64_add_i(reg_a, reg_b, imm12);
        (void)encoded;
        encoded = a64_sub_i(reg_a, reg_b, imm12);
        (void)encoded;
        encoded = a64_and_r(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_orr_r(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_eor_r(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_lsl(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_lsr(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_asr(reg_a, reg_b, reg_c);
        (void)encoded;
        encoded = a64_cset(reg_a, reg_b & 0xf);
        (void)encoded;
        encoded = a64_b(offset);
        (void)encoded;
        encoded = a64_bl(offset);
        (void)encoded;
        encoded = a64_b_cond(reg_a & 0xf, offset);
        (void)encoded;
        encoded = a64_br(reg_a);
        (void)encoded;
        encoded = a64_blr(reg_a);
        (void)encoded;
        encoded = a64_ret();
        (void)encoded;
        encoded = a64_ldr(reg_a, reg_b, offset & ~7);
        (void)encoded;
        encoded = a64_str(reg_a, reg_b, offset & ~7);
        (void)encoded;
        encoded = a64_ldrb(reg_a, reg_b, offset);
        (void)encoded;
        encoded = a64_strb(reg_a, reg_b, offset);
        (void)encoded;
        encoded = a64_ldrh(reg_a, reg_b, offset & ~1);
        (void)encoded;
        encoded = a64_strh(reg_a, reg_b, offset & ~1);
        (void)encoded;
        encoded = a64_adrp(reg_a, offset);
        (void)encoded;
        encoded = a64_adr(reg_a, offset);
        (void)encoded;
        encoded = a64_mvn(reg_a, reg_b);
        (void)encoded;
        encoded = a64_sxtb(reg_a, reg_b);
        (void)encoded;
        encoded = a64_sxth(reg_a, reg_b);
        (void)encoded;
        encoded = a64_sxtw(reg_a, reg_b);
        (void)encoded;
        encoded = a64_uxtb(reg_a, reg_b);
        (void)encoded;
        encoded = a64_uxth(reg_a, reg_b);
        (void)encoded;
        encoded = a64_stp_pre(reg_a, reg_b, reg_c, offset & ~7);
        (void)encoded;
        encoded = a64_ldp_post(reg_a, reg_b, reg_c, offset & ~7);
        (void)encoded;
        encoded = a64_stp(reg_a, reg_b, reg_c, offset & ~7);
        (void)encoded;
        encoded = a64_ldp(reg_a, reg_b, reg_c, offset & ~7);
        (void)encoded;
    }

    free(input);
    return 0;
}
