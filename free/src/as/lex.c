/*
 * lex.c - Assembly tokenizer for the free toolchain
 * Tokenizes aarch64 assembly source into tokens.
 * Pure C89. No external dependencies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "free.h"
#include "aarch64.h"

/* ---- Assembly token types ---- */
enum as_tok_kind {
    ASTOK_EOF = 0,
    ASTOK_NEWLINE,
    ASTOK_IDENT,       /* instruction mnemonic or unknown identifier */
    ASTOK_LABEL,       /* identifier followed by colon (name:) */
    ASTOK_DIRECTIVE,   /* .global, .text, etc */
    ASTOK_REG,         /* register: x0-x30, w0-w30, sp, xzr, etc */
    ASTOK_FPREG,       /* FP/SIMD register: d0-d31, s0-s31 */
    ASTOK_IMM,         /* immediate: #N or #0xN */
    ASTOK_NUM,         /* bare number (for directive arguments) */
    ASTOK_STRING,      /* quoted string */
    ASTOK_COMMA,
    ASTOK_LBRACKET,
    ASTOK_RBRACKET,
    ASTOK_EXCL,        /* ! for pre-index */
    ASTOK_COLON,
    ASTOK_MINUS,
    ASTOK_LPAREN,
    ASTOK_RPAREN,
    ASTOK_EQ,          /* == */
    ASTOK_NE,          /* != */
    ASTOK_LSHIFT,      /* << */
    ASTOK_RSHIFT,      /* >> */
    ASTOK_PIPE,        /* | */
    ASTOK_AMPERSAND,   /* & */
    ASTOK_TILDE,       /* ~ */
    ASTOK_PLUS,
    ASTOK_SLASH,       /* / (division) */
    ASTOK_STAR         /* * (multiplication) */
};

struct as_token {
    enum as_tok_kind kind;
    const char *start;
    int len;
    long val;          /* numeric value for IMM/NUM, register number for REG */
    int is_wreg;       /* 1 if register is w-form (32-bit) */
    int is_sreg;       /* 1 if FP register is s-form (single precision) */
    int line;
};

struct as_lexer {
    const char *src;
    const char *pos;
    int line;
};

/* ---- Forward declarations ---- */
static int is_alpha(char c);
static int is_digit(char c);
static int is_alnum(char c);
static int is_space(char c);
static int is_hex(char c);
static int hex_val(char c);
static void skip_whitespace(struct as_lexer *lex);
static void skip_line_comment(struct as_lexer *lex);
static int skip_block_comment(struct as_lexer *lex);
static int parse_register(const char *name, int len, int *is_wreg);
static long parse_number(const char *s, int len);
static int all_digits(const char *s, int len);

/* ---- Character classification ---- */

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int is_alnum(char c)
{
    return is_alpha(c) || is_digit(c);
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static int is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return 0;
}

/* ---- Whitespace and comment handling ---- */

static void skip_whitespace(struct as_lexer *lex)
{
    while (is_space(*lex->pos)) {
        lex->pos++;
    }
}

static void skip_line_comment(struct as_lexer *lex)
{
    while (*lex->pos && *lex->pos != '\n') {
        lex->pos++;
    }
}

static int skip_block_comment(struct as_lexer *lex)
{
    /* skip past opening / * */
    lex->pos += 2;
    while (*lex->pos) {
        if (*lex->pos == '\n') {
            lex->line++;
        }
        if (lex->pos[0] == '*' && lex->pos[1] == '/') {
            lex->pos += 2;
            return 1;
        }
        lex->pos++;
    }
    return 0; /* unterminated comment */
}

/* ---- Number parsing ---- */

static long parse_number(const char *s, int len)
{
    long val = 0;
    int i = 0;
    int neg = 0;

    if (i < len && s[i] == '-') {
        neg = 1;
        i++;
    }

    if (i + 1 < len && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        /* hex */
        i += 2;
        while (i < len && is_hex(s[i])) {
            val = val * 16 + hex_val(s[i]);
            i++;
        }
    } else {
        /* decimal */
        while (i < len && is_digit(s[i])) {
            val = val * 10 + (s[i] - '0');
            i++;
        }
    }

    return neg ? -val : val;
}

/* ---- Register name lookup ---- */

static int streqn(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static int parse_register(const char *name, int len, int *is_wreg)
{
    long num;

    *is_wreg = 0;

    /* x0-x30 */
    if (len >= 2 && len <= 3 && name[0] == 'x' &&
        all_digits(name + 1, len - 1)) {
        num = parse_number(name + 1, len - 1);
        if (num >= 0 && num <= 30) {
            return (int)num;
        }
    }

    /* w0-w30 */
    if (len >= 2 && len <= 3 && name[0] == 'w' &&
        all_digits(name + 1, len - 1)) {
        num = parse_number(name + 1, len - 1);
        if (num >= 0 && num <= 30) {
            *is_wreg = 1;
            return (int)num;
        }
    }

    /* sp */
    if (len == 2 && streqn(name, "sp", 2)) {
        return REG_SP;
    }

    /* xzr */
    if (len == 3 && streqn(name, "xzr", 3)) {
        return REG_XZR;
    }

    /* wzr */
    if (len == 3 && streqn(name, "wzr", 3)) {
        *is_wreg = 1;
        return REG_XZR;
    }

    /* fp = x29 */
    if (len == 2 && streqn(name, "fp", 2)) {
        return REG_FP;
    }

    /* lr = x30 */
    if (len == 2 && streqn(name, "lr", 2)) {
        return REG_LR;
    }

    return -1; /* not a register */
}

static int all_digits(const char *s, int len)
{
    int i;
    if (len <= 0) return 0;
    for (i = 0; i < len; i++) {
        if (!is_digit(s[i])) return 0;
    }
    return 1;
}

static int parse_fp_register(const char *name, int len, int *is_sreg)
{
    long num;

    *is_sreg = 0;

    /* d0-d31 (double) */
    if (len >= 2 && len <= 3 && name[0] == 'd' &&
        all_digits(name + 1, len - 1)) {
        num = parse_number(name + 1, len - 1);
        if (num >= 0 && num <= 31) {
            return (int)num;
        }
    }

    /* s0-s31 (single) */
    if (len >= 2 && len <= 3 && name[0] == 's' &&
        all_digits(name + 1, len - 1)) {
        num = parse_number(name + 1, len - 1);
        if (num >= 0 && num <= 31) {
            *is_sreg = 1;
            return (int)num;
        }
    }

    return -1;
}

/* ---- Lexer API ---- */

void as_lex_init(struct as_lexer *lex, const char *src)
{
    lex->src = src;
    lex->pos = src;
    lex->line = 1;
}

void as_next_token(struct as_lexer *lex, struct as_token *tok)
{
    const char *start;
    int len;
    int reg;
    int is_w;

    /* zero out token */
    tok->kind = ASTOK_EOF;
    tok->start = NULL;
    tok->len = 0;
    tok->val = 0;
    tok->is_wreg = 0;
    tok->is_sreg = 0;
    tok->line = lex->line;

again:
    skip_whitespace(lex);

    if (*lex->pos == '\0') {
        tok->kind = ASTOK_EOF;
        tok->line = lex->line;
        return;
    }

    tok->line = lex->line;

    /* newline */
    if (*lex->pos == '\n') {
        tok->kind = ASTOK_NEWLINE;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        lex->line++;
        return;
    }

    /* line comment: // or @ (gas-style) */
    if ((lex->pos[0] == '/' && lex->pos[1] == '/') || lex->pos[0] == '@') {
        skip_line_comment(lex);
        goto again;
    }

    /* block comment */
    if (lex->pos[0] == '/' && lex->pos[1] == '*') {
        skip_block_comment(lex);
        goto again;
    }

    /* standalone / (division, not comment) */
    if (lex->pos[0] == '/' && lex->pos[1] != '/' && lex->pos[1] != '*') {
        tok->kind = ASTOK_SLASH;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* * (multiplication) */
    if (lex->pos[0] == '*') {
        tok->kind = ASTOK_STAR;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* semicolon as statement separator (treat like newline) */
    if (*lex->pos == ';') {
        tok->kind = ASTOK_NEWLINE;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* comma */
    if (*lex->pos == ',') {
        tok->kind = ASTOK_COMMA;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* brackets */
    if (*lex->pos == '[') {
        tok->kind = ASTOK_LBRACKET;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }
    if (*lex->pos == ']') {
        tok->kind = ASTOK_RBRACKET;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* exclamation mark (pre-index) or != */
    if (*lex->pos == '!') {
        if (lex->pos[1] == '=') {
            tok->kind = ASTOK_NE;
            tok->start = lex->pos;
            tok->len = 2;
            lex->pos += 2;
            return;
        }
        tok->kind = ASTOK_EXCL;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* == (equality comparison) */
    if (lex->pos[0] == '=' && lex->pos[1] == '=') {
        tok->kind = ASTOK_EQ;
        tok->start = lex->pos;
        tok->len = 2;
        lex->pos += 2;
        return;
    }

    /* parentheses */
    if (*lex->pos == '(') {
        tok->kind = ASTOK_LPAREN;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }
    if (*lex->pos == ')') {
        tok->kind = ASTOK_RPAREN;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* << and >> */
    if (lex->pos[0] == '<' && lex->pos[1] == '<') {
        tok->kind = ASTOK_LSHIFT;
        tok->start = lex->pos;
        tok->len = 2;
        lex->pos += 2;
        return;
    }
    if (lex->pos[0] == '>' && lex->pos[1] == '>') {
        tok->kind = ASTOK_RSHIFT;
        tok->start = lex->pos;
        tok->len = 2;
        lex->pos += 2;
        return;
    }

    /* | (bitwise or) */
    if (*lex->pos == '|') {
        tok->kind = ASTOK_PIPE;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* & (bitwise and) */
    if (*lex->pos == '&') {
        tok->kind = ASTOK_AMPERSAND;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* ~ (bitwise not) */
    if (*lex->pos == '~') {
        tok->kind = ASTOK_TILDE;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* + */
    if (*lex->pos == '+') {
        tok->kind = ASTOK_PLUS;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* colon (not part of a label — standalone) */
    if (*lex->pos == ':') {
        tok->kind = ASTOK_COLON;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* immediate: #number or #-number; or line comment if # not followed
     * by digit/minus (preprocessor line directives, comment syntax) */
    if (*lex->pos == '#') {
        char next = lex->pos[1];
        if (is_digit(next) || next == '-' ||
            (next == '0' && (lex->pos[2] == 'x' || lex->pos[2] == 'X'))) {
            /* genuine immediate */
            lex->pos++; /* skip # */
            start = lex->pos;
            if (*lex->pos == '-') {
                lex->pos++;
            }
            if (lex->pos[0] == '0' &&
                (lex->pos[1] == 'x' || lex->pos[1] == 'X')) {
                lex->pos += 2;
                while (is_hex(*lex->pos)) lex->pos++;
            } else {
                while (is_digit(*lex->pos)) lex->pos++;
            }
            len = (int)(lex->pos - start);
            tok->kind = ASTOK_IMM;
            tok->start = start;
            tok->len = len;
            tok->val = parse_number(start, len);
            return;
        }
        /* treat # as line comment (preprocessor directives, comments) */
        skip_line_comment(lex);
        goto again;
    }

    /* minus sign (for negative offsets in directives) */
    if (*lex->pos == '-' && is_digit(lex->pos[1])) {
        start = lex->pos;
        lex->pos++;
        if (lex->pos[0] == '0' && (lex->pos[1] == 'x' || lex->pos[1] == 'X')) {
            lex->pos += 2;
            while (is_hex(*lex->pos)) lex->pos++;
        } else {
            while (is_digit(*lex->pos)) lex->pos++;
        }
        len = (int)(lex->pos - start);
        tok->kind = ASTOK_NUM;
        tok->start = start;
        tok->len = len;
        tok->val = parse_number(start, len);
        return;
    }

    if (*lex->pos == '-') {
        tok->kind = ASTOK_MINUS;
        tok->start = lex->pos;
        tok->len = 1;
        lex->pos++;
        return;
    }

    /* bare number, numeric labels (1:), numeric label refs (1f/1b) */
    if (is_digit(*lex->pos)) {
        start = lex->pos;
        if (lex->pos[0] == '0' && (lex->pos[1] == 'x' || lex->pos[1] == 'X')) {
            lex->pos += 2;
            while (is_hex(*lex->pos)) lex->pos++;
        } else {
            while (is_digit(*lex->pos)) lex->pos++;
        }
        len = (int)(lex->pos - start);
        /* numeric label: digits followed by colon (e.g. 1:, 661:) */
        if (*lex->pos == ':') {
            lex->pos++; /* consume colon */
            tok->kind = ASTOK_LABEL;
            tok->start = start;
            tok->len = len;
            tok->val = parse_number(start, len);
            return;
        }
        /* numeric label ref: digits followed by f or b (e.g. 1f, 662b) */
        if (*lex->pos == 'f' || *lex->pos == 'b') {
            tok->kind = ASTOK_IDENT;
            tok->start = start;
            tok->len = len + 1; /* digits + direction */
            tok->val = parse_number(start, len);
            lex->pos++; /* consume f/b */
            return;
        }
        tok->kind = ASTOK_NUM;
        tok->start = start;
        tok->len = len;
        tok->val = parse_number(start, len);
        return;
    }

    /* directive: .name, or standalone dot (current position) */
    if (*lex->pos == '.') {
        start = lex->pos;
        lex->pos++;
        if (is_alnum(*lex->pos)) {
            while (is_alnum(*lex->pos) || *lex->pos == '_') lex->pos++;
            len = (int)(lex->pos - start);
            /* .LF0: is a label, not a directive */
            if (*lex->pos == ':') {
                lex->pos++; /* consume colon */
                tok->kind = ASTOK_LABEL;
                tok->start = start;
                tok->len = len;
                return;
            }
            tok->kind = ASTOK_DIRECTIVE;
            tok->start = start;
            tok->len = len;
            return;
        }
        /* standalone dot = current position */
        tok->kind = ASTOK_IDENT;
        tok->start = start;
        tok->len = 1;
        return;
    }

    /* string literal */
    if (*lex->pos == '"') {
        lex->pos++; /* skip opening quote */
        start = lex->pos;
        while (*lex->pos && *lex->pos != '"') {
            if (*lex->pos == '\\') {
                lex->pos++; /* skip escape char */
            }
            if (*lex->pos == '\n') {
                lex->line++;
            }
            if (*lex->pos) {
                lex->pos++;
            }
        }
        len = (int)(lex->pos - start);
        tok->kind = ASTOK_STRING;
        tok->start = start;
        tok->len = len;
        if (*lex->pos == '"') {
            lex->pos++; /* skip closing quote */
        }
        return;
    }

    /* identifier, register, instruction mnemonic, or label */
    if (is_alpha(*lex->pos)) {
        start = lex->pos;
        while (is_alnum(*lex->pos) || *lex->pos == '.') {
            lex->pos++;
        }
        len = (int)(lex->pos - start);

        /* check if followed by colon => label */
        if (*lex->pos == ':') {
            lex->pos++; /* consume colon */
            tok->kind = ASTOK_LABEL;
            tok->start = start;
            tok->len = len;
            return;
        }

        /* check if it's a register */
        reg = parse_register(start, len, &is_w);
        if (reg >= 0) {
            tok->kind = ASTOK_REG;
            tok->start = start;
            tok->len = len;
            tok->val = reg;
            tok->is_wreg = is_w;
            return;
        }

        /* check if it's an FP register */
        {
            int is_s;
            reg = parse_fp_register(start, len, &is_s);
            if (reg >= 0) {
                tok->kind = ASTOK_FPREG;
                tok->start = start;
                tok->len = len;
                tok->val = reg;
                tok->is_sreg = is_s;
                return;
            }
        }

        /* otherwise it's an identifier (mnemonic, symbol name, etc) */
        tok->kind = ASTOK_IDENT;
        tok->start = start;
        tok->len = len;
        return;
    }

    /* skip unknown characters */
    lex->pos++;
    goto again;
}
