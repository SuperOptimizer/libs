/*
 * lex.c - Tokenizer for the free C compiler.
 * Supports C89, C99, C11, and C23 tokens based on feature flags.
 * Pure C89. All variables at top of block.
 */

#include "free.h"
#include <string.h>

/* ---- static state ---- */
static const char *lex_src;
static const char *lex_pos;
static const char *lex_filename;
static int lex_line;
static int lex_col;
static struct tok lex_ahead;
static int lex_has_ahead;

/* ---- token arena ---- */
static struct arena *lex_arena;

/* ---- keyword table ---- */
struct kw_entry {
    const char *name;
    enum tok_kind kind;
};

static const struct kw_entry keywords[] = {
    { "auto",     TOK_AUTO },
    { "break",    TOK_BREAK },
    { "case",     TOK_CASE },
    { "char",     TOK_CHAR_KW },
    { "const",    TOK_CONST },
    { "continue", TOK_CONTINUE },
    { "default",  TOK_DEFAULT },
    { "do",       TOK_DO },
    { "double",   TOK_DOUBLE },
    { "else",     TOK_ELSE },
    { "enum",     TOK_ENUM },
    { "extern",   TOK_EXTERN },
    { "float",    TOK_FLOAT },
    { "for",      TOK_FOR },
    { "goto",     TOK_GOTO },
    { "if",       TOK_IF },
    { "int",      TOK_INT },
    { "long",     TOK_LONG },
    { "register", TOK_REGISTER },
    { "return",   TOK_RETURN },
    { "short",    TOK_SHORT },
    { "signed",   TOK_SIGNED },
    { "sizeof",   TOK_SIZEOF },
    { "static",   TOK_STATIC },
    { "struct",   TOK_STRUCT },
    { "switch",   TOK_SWITCH },
    { "typedef",  TOK_TYPEDEF },
    { "union",    TOK_UNION },
    { "unsigned", TOK_UNSIGNED },
    { "void",     TOK_VOID },
    { "volatile", TOK_VOLATILE },
    { "while",    TOK_WHILE },
    { NULL,       TOK_EOF }
};

/* ---- C99/C11/C23 extended keyword entries ---- */
struct ext_kw_entry {
    const char *name;
    enum tok_kind kind;
    unsigned long feat;  /* FEAT_* flag required, 0 = always */
    unsigned long feat2; /* FEAT2_* flag, 0 = use feat */
};

static const struct ext_kw_entry ext_keywords[] = {
    /* GCC alternate keywords (always available) */
    { "__signed__",     TOK_SIGNED,        0, 0 },
    { "__signed",       TOK_SIGNED,        0, 0 },
    { "__unsigned__",   TOK_UNSIGNED,      0, 0 },
    { "__unsigned",     TOK_UNSIGNED,      0, 0 },
    { "__volatile__",   TOK_VOLATILE,      0, 0 },
    { "__volatile",     TOK_VOLATILE,      0, 0 },
    { "__const__",      TOK_CONST,         0, 0 },
    { "__const",        TOK_CONST,         0, 0 },
    /* C99 keywords */
    { "_Bool",          TOK_BOOL,          FEAT_BOOL, 0 },
    { "_Complex",       TOK_COMPLEX,       0, FEAT2_COMPLEX },
    { "__complex__",    TOK_COMPLEX,       0, FEAT2_COMPLEX },
    { "__complex",      TOK_COMPLEX,       0, FEAT2_COMPLEX },
    /* GCC decimal floating-point types (map to float/double) */
    { "_Decimal32",     TOK_FLOAT,         0, 0 },
    { "_Decimal64",     TOK_DOUBLE,        0, 0 },
    { "_Decimal128",    TOK_DOUBLE,        0, 0 },
    { "restrict",       TOK_RESTRICT,      FEAT_RESTRICT, 0 },
    { "__restrict",     TOK_RESTRICT,      FEAT_RESTRICT, 0 },
    { "__restrict__",   TOK_RESTRICT,      FEAT_RESTRICT, 0 },
    { "inline",         TOK_INLINE,        FEAT_INLINE, 0 },
    { "__inline",       TOK_INLINE,        FEAT_INLINE, 0 },
    { "__inline__",     TOK_INLINE,        FEAT_INLINE, 0 },
    /* C11 keywords */
    { "_Alignas",       TOK_ALIGNAS,       FEAT_ALIGNAS, 0 },
    { "_Alignof",       TOK_ALIGNOF,       FEAT_ALIGNOF, 0 },
    { "_Static_assert", TOK_STATIC_ASSERT, FEAT_STATIC_ASSERT, 0 },
    { "_Noreturn",      TOK_NORETURN,      FEAT_NORETURN, 0 },
    { "_Generic",       TOK_GENERIC,       FEAT_GENERIC, 0 },
    { "_Atomic",        TOK_ATOMIC,        FEAT_ATOMIC, 0 },
    { "_Thread_local",  TOK_THREAD_LOCAL,  FEAT_THREAD_LOCAL, 0 },
    /* C23 keywords */
    { "true",           TOK_TRUE,          FEAT_BOOL_KW, 0 },
    { "false",          TOK_FALSE,         FEAT_BOOL_KW, 0 },
    { "bool",           TOK_BOOL_KW,       FEAT_BOOL_KW, 0 },
    { "nullptr",        TOK_NULLPTR,       FEAT_NULLPTR, 0 },
    { "typeof",         TOK_TYPEOF,        FEAT_TYPEOF, 0 },
    { "typeof_unqual",  TOK_TYPEOF_UNQUAL, FEAT_TYPEOF, 0 },
    { "constexpr",      TOK_CONSTEXPR,     0, FEAT2_CONSTEXPR },
    { "static_assert",  TOK_STATIC_ASSERT_KW, 0, FEAT2_STATIC_ASSERT_NS },
    { NULL,             TOK_EOF,           0, 0 }
};

/* ---- helpers ---- */

static int is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_alnum(int c)
{
    return is_alpha(c) || is_digit(c);
}

static int is_hex(int c)
{
    return is_digit(c)
        || (c >= 'a' && c <= 'f')
        || (c >= 'A' && c <= 'F');
}

static int is_octal(int c)
{
    return c >= '0' && c <= '7';
}

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/* skip_bsnl - advance past any backslash-newline sequences at lex_pos.
 * This implements C line splicing (translation phase 2):
 * a backslash immediately followed by a newline is deleted,
 * logically joining the two physical lines. */
static void skip_bsnl(void)
{
    while (lex_pos[0] == '\\' && lex_pos[1] == '\n') {
        lex_pos += 2;
        lex_line++;
        lex_col = 1;
    }
}

static char *copy_spelling(const char *start, const char *end)
{
    const char *p;
    char *s;
    char *q;
    int len;

    len = 0;
    p = start;
    while (p < end) {
        if (p[0] == '\\' && p + 1 < end && p[1] == '\n') {
            p += 2;
            continue;
        }
        len++;
        p++;
    }

    s = (char *)arena_alloc(lex_arena, (usize)len + 1);
    q = s;
    p = start;
    while (p < end) {
        if (p[0] == '\\' && p + 1 < end && p[1] == '\n') {
            p += 2;
            continue;
        }
        *q++ = *p++;
    }
    *q = '\0';
    return s;
}

static char peek_ch(void)
{
    skip_bsnl();
    return *lex_pos;
}

static char next_ch(void)
{
    char c;

    skip_bsnl();
    c = *lex_pos;
    if (c == '\0') {
        return '\0';
    }
    lex_pos++;
    if (c == '\n') {
        lex_line++;
        lex_col = 1;
    } else {
        lex_col++;
    }
    return c;
}

static char peek_ch2(void)
{
    const char *p;

    skip_bsnl();
    if (*lex_pos == '\0') {
        return '\0';
    }
    /* look past the next character, also skipping any bsnl after it */
    p = lex_pos + 1;
    while (p[0] == '\\' && p[1] == '\n') {
        p += 2;
    }
    return *p;
}

static struct tok *make_tok(enum tok_kind kind, int line, int col)
{
    struct tok *t;

    t = (struct tok *)arena_alloc(lex_arena, sizeof(struct tok));
    t->kind = kind;
    t->val = 0;
    t->fval = 0.0;
    t->str = NULL;
    t->raw = NULL;
    t->len = 0;
    t->file = lex_filename;
    t->line = line;
    t->col = col;
    t->suffix_unsigned = 0;
    t->suffix_long = 0;
    t->suffix_float = 0;
    t->suffix_imaginary = 0;
    t->is_hex_or_oct = 0;
    t->no_expand = 0;
    return t;
}

/* ---- skip whitespace and comments ---- */

static void skip_whitespace(void)
{
    for (;;) {
        /* skip spaces, tabs, etc (not newlines for pp) */
        while (is_space(peek_ch()) || peek_ch() == '\n') {
            next_ch();
        }

        /* block comment */
        if (peek_ch() == '/' && peek_ch2() == '*') {
            next_ch();
            next_ch();
            for (;;) {
                if (peek_ch() == '\0') {
                    err_at(lex_filename, lex_line, lex_col,
                           "unterminated block comment");
                    return;
                }
                if (peek_ch() == '*' && peek_ch2() == '/') {
                    next_ch();
                    next_ch();
                    break;
                }
                next_ch();
            }
            continue;
        }

        /* line comment (extension, but handle gracefully) */
        if (peek_ch() == '/' && peek_ch2() == '/') {
            next_ch();
            next_ch();
            while (peek_ch() != '\0' && peek_ch() != '\n') {
                next_ch();
            }
            continue;
        }

        break;
    }
}

/* ---- read escape character ---- */

static int read_escape(void)
{
    char c;
    int val;
    int i;

    c = next_ch(); /* consume char after backslash */
    switch (c) {
    case 'a':  return '\a';
    case 'b':  return '\b';
    case 'f':  return '\f';
    case 'n':  return '\n';
    case 'r':  return '\r';
    case 't':  return '\t';
    case 'v':  return '\v';
    case '\\': return '\\';
    case '\'': return '\'';
    case '\"': return '\"';
    case '?':  return '?';
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7':
        /* octal escape: up to 3 digits */
        val = c - '0';
        for (i = 1; i < 3 && is_octal(peek_ch()); i++) {
            val = val * 8 + (next_ch() - '0');
        }
        return val;
    case 'x':
        /* hex escape */
        val = 0;
        if (!is_hex(peek_ch())) {
            err_at(lex_filename, lex_line, lex_col,
                   "invalid hex escape sequence");
            return 0;
        }
        while (is_hex(peek_ch())) {
            c = next_ch();
            if (c >= '0' && c <= '9') {
                val = val * 16 + (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                val = val * 16 + (c - 'a' + 10);
            } else {
                val = val * 16 + (c - 'A' + 10);
            }
        }
        return val;
    default:
        return c;
    }
}

/*
 * parse_frac_digits - parse fractional digits accurately.
 * Collects digits, computes value as integer/10^n to avoid
 * cumulative rounding errors from repeated 0.1 multiplication.
 */
static double parse_frac_digits(void)
{
    double frac_int;
    double divisor;
    int count;

    frac_int = 0.0;
    divisor = 1.0;
    count = 0;
    while (is_digit(peek_ch())) {
        frac_int = frac_int * 10.0 + (next_ch() - '0');
        divisor *= 10.0;
        count++;
    }
    if (count == 0) {
        return 0.0;
    }
    return frac_int / divisor;
}

/* ---- read number literal ---- */

static struct tok *read_number(void)
{
    struct tok *t;
    int line, col;
    const char *start;
    long val;
    char c;
    int is_hex_oct;

    line = lex_line;
    col = lex_col;
    start = lex_pos;
    val = 0;
    is_hex_oct = 0;

    /* binary literal: 0b or 0B (C23) */
    if (peek_ch() == '0' &&
        (peek_ch2() == 'b' || peek_ch2() == 'B') &&
        cc_has_feat(FEAT_BIN_LITERAL)) {
        next_ch(); /* '0' */
        next_ch(); /* 'b' */
        if (peek_ch() != '0' && peek_ch() != '1') {
            err_at(lex_filename, line, col, "invalid binary literal");
        }
        while (peek_ch() == '0' || peek_ch() == '1' ||
               (peek_ch() == '\'' && cc_has_feat(FEAT_DIGIT_SEP))) {
            c = next_ch();
            if (c == '\'') {
                continue; /* digit separator */
            }
            val = val * 2 + (c - '0');
        }
    } else if (peek_ch() == '0' &&
               (peek_ch2() == 'x' || peek_ch2() == 'X')) {
        /* hex */
        is_hex_oct = 1;
        next_ch(); /* '0' */
        next_ch(); /* 'x' */
        if (!is_hex(peek_ch()) && peek_ch() != '.') {
            err_at(lex_filename, line, col, "invalid hex literal");
        }
        while (is_hex(peek_ch()) ||
               (peek_ch() == '\'' && cc_has_feat(FEAT_DIGIT_SEP))) {
            c = next_ch();
            if (c == '\'') {
                continue; /* digit separator */
            }
            if (c >= '0' && c <= '9') {
                val = val * 16 + (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                val = val * 16 + (c - 'a' + 10);
            } else {
                val = val * 16 + (c - 'A' + 10);
            }
        }
        /* hex float: check for '.' or 'p'/'P' */
        if ((peek_ch() == '.' || peek_ch() == 'p' || peek_ch() == 'P')
            && cc_has_feat(FEAT_HEX_FLOAT)) {
            double hf_val;
            double hf_frac;
            int hf_exp;
            int hf_exp_sign;

            hf_val = (double)val;
            hf_frac = 1.0;
            if (peek_ch() == '.') {
                next_ch(); /* '.' */
                while (is_hex(peek_ch())) {
                    int d;
                    c = next_ch();
                    if (c >= '0' && c <= '9') d = c - '0';
                    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                    else d = c - 'A' + 10;
                    hf_frac /= 16.0;
                    hf_val += d * hf_frac;
                }
            }
            hf_exp = 0;
            hf_exp_sign = 1;
            if (peek_ch() == 'p' || peek_ch() == 'P') {
                next_ch(); /* 'p' */
                if (peek_ch() == '+') {
                    next_ch();
                } else if (peek_ch() == '-') {
                    next_ch();
                    hf_exp_sign = -1;
                }
                while (is_digit(peek_ch())) {
                    hf_exp = hf_exp * 10 + (next_ch() - '0');
                }
            }
            /* apply binary exponent: val * 2^exp */
            {
                int ei;
                if (hf_exp_sign > 0) {
                    for (ei = 0; ei < hf_exp; ei++)
                        hf_val *= 2.0;
                } else {
                    for (ei = 0; ei < hf_exp; ei++)
                        hf_val /= 2.0;
                }
            }
            /* skip float suffix and GNU imaginary suffix (any order) */
            {
                int hf_is_imag = 0;
                int hf_is_float = 0;
                int hf_is_long = 0;
                if (peek_ch() == 'i' || peek_ch() == 'j') {
                    next_ch();
                    hf_is_imag = 1;
                }
                if (peek_ch() == 'f' || peek_ch() == 'F') {
                    hf_is_float = 1;
                    next_ch();
                } else if (peek_ch() == 'l' || peek_ch() == 'L') {
                    hf_is_long = 1;
                    next_ch();
                }
                if (peek_ch() == 'i' || peek_ch() == 'j') {
                    next_ch();
                    hf_is_imag = 1;
                }
                t = make_tok(TOK_FNUM, line, col);
                t->fval = hf_val;
                t->suffix_float = hf_is_float;
                t->suffix_long = hf_is_long;
                t->suffix_imaginary = hf_is_imag;
                t->raw = copy_spelling(start, lex_pos);
                return t;
            }
        }
    } else if (peek_ch() == '0') {
        /* octal or 0.xxx float */
        is_hex_oct = 1;
        next_ch(); /* '0' */
        while (is_octal(peek_ch()) ||
               (peek_ch() == '\'' && cc_has_feat(FEAT_DIGIT_SEP))) {
            c = next_ch();
            if (c == '\'') {
                continue;
            }
            val = val * 8 + (c - '0');
        }
        /* check for floating-point: 0.x or 0e */
        if (peek_ch() == '.' || peek_ch() == 'e' || peek_ch() == 'E') {
            double fv;
            double exp_val;
            int exp_sign;
            int exp_num;
            int is_float_suffix;
            int is_long_suffix;
            int is_imaginary;

            fv = (double)val;
            if (peek_ch() == '.') {
                next_ch(); /* '.' */
                fv += parse_frac_digits();
            }
            if (peek_ch() == 'e' || peek_ch() == 'E') {
                next_ch();
                exp_sign = 1;
                exp_num = 0;
                if (peek_ch() == '+') {
                    next_ch();
                } else if (peek_ch() == '-') {
                    next_ch();
                    exp_sign = -1;
                }
                while (is_digit(peek_ch())) {
                    exp_num = exp_num * 10 + (next_ch() - '0');
                }
                exp_val = 1.0;
                while (exp_num > 0) {
                    exp_val *= 10.0;
                    exp_num--;
                }
                if (exp_sign < 0) {
                    fv /= exp_val;
                } else {
                    fv *= exp_val;
                }
            }
            is_float_suffix = 0;
            is_long_suffix = 0;
            is_imaginary = 0;
            /* GNU imaginary suffix before type suffix (e.g. 0.0iF) */
            if (peek_ch() == 'i' || peek_ch() == 'j') {
                next_ch();
                is_imaginary = 1;
            }
            if (peek_ch() == 'f' || peek_ch() == 'F') {
                is_float_suffix = 1;
                next_ch();
            } else if (peek_ch() == 'l' || peek_ch() == 'L') {
                is_long_suffix = 1;
                next_ch();
            } else if (peek_ch() == 'd' || peek_ch() == 'D') {
                /* GCC decimal float suffix: DD, DF, DL */
                next_ch();
                if (peek_ch() == 'd' || peek_ch() == 'D' ||
                    peek_ch() == 'f' || peek_ch() == 'F' ||
                    peek_ch() == 'l' || peek_ch() == 'L') {
                    next_ch();
                }
            }
            /* GNU imaginary suffix after type suffix (e.g. 0.0fi) */
            if (peek_ch() == 'i' || peek_ch() == 'j') {
                next_ch();
                is_imaginary = 1;
            }
            t = make_tok(TOK_FNUM, line, col);
            t->fval = fv;
            t->suffix_float = is_float_suffix;
            t->suffix_long = is_long_suffix;
            t->suffix_imaginary = is_imaginary;
            return t;
        }
    } else {
        /* decimal */
        while (is_digit(peek_ch()) ||
               (peek_ch() == '\'' && cc_has_feat(FEAT_DIGIT_SEP))) {
            c = next_ch();
            if (c == '\'') {
                continue;
            }
            val = val * 10 + (c - '0');
        }

        /* check for floating-point: '.', 'e', 'E' */
        if (peek_ch() == '.' || peek_ch() == 'e' || peek_ch() == 'E') {
            double fv;
            double exp_val;
            int exp_sign;
            int exp_num;
            int is_float_suffix;
            int is_long_suffix;
            int is_imaginary;

            fv = (double)val;

            if (peek_ch() == '.') {
                next_ch(); /* '.' */
                fv += parse_frac_digits();
            }

            if (peek_ch() == 'e' || peek_ch() == 'E') {
                next_ch(); /* 'e' */
                exp_sign = 1;
                exp_num = 0;
                if (peek_ch() == '+') {
                    next_ch();
                } else if (peek_ch() == '-') {
                    next_ch();
                    exp_sign = -1;
                }
                while (is_digit(peek_ch())) {
                    exp_num = exp_num * 10 + (next_ch() - '0');
                }
                exp_val = 1.0;
                while (exp_num > 0) {
                    exp_val *= 10.0;
                    exp_num--;
                }
                if (exp_sign < 0) {
                    fv /= exp_val;
                } else {
                    fv *= exp_val;
                }
            }

            /* check for float/imaginary suffixes (any order) */
            is_float_suffix = 0;
            is_long_suffix = 0;
            is_imaginary = 0;
            /* GNU imaginary suffix before type suffix (e.g. 1.0iF) */
            if (peek_ch() == 'i' || peek_ch() == 'j') {
                next_ch();
                is_imaginary = 1;
            }
            if (peek_ch() == 'f' || peek_ch() == 'F') {
                is_float_suffix = 1;
                next_ch();
            } else if (peek_ch() == 'l' || peek_ch() == 'L') {
                is_long_suffix = 1;
                next_ch();
            } else if (peek_ch() == 'd' || peek_ch() == 'D') {
                /* GCC decimal float suffix: DD, DF, DL */
                next_ch();
                if (peek_ch() == 'd' || peek_ch() == 'D' ||
                    peek_ch() == 'f' || peek_ch() == 'F' ||
                    peek_ch() == 'l' || peek_ch() == 'L') {
                    next_ch();
                }
            }
            /* GNU imaginary suffix after type suffix (e.g. 1.0fi) */
            if (peek_ch() == 'i' || peek_ch() == 'j') {
                next_ch();
                is_imaginary = 1;
            }

            t = make_tok(TOK_FNUM, line, col);
            t->fval = fv;
            t->suffix_float = is_float_suffix;
            t->suffix_long = is_long_suffix;
            t->suffix_imaginary = is_imaginary;
            t->raw = copy_spelling(start, lex_pos);
            return t;
        }
    }

    /* GCC extension: integer with 'f'/'F' suffix => float literal
     * e.g. 1f means 1.0f, 8f means 8.0f (only for decimal literals) */
    if (!is_hex_oct && (peek_ch() == 'f' || peek_ch() == 'F')) {
        int fi_is_imag;
        fi_is_imag = 0;
        next_ch();
        /* GNU imaginary suffix */
        if (peek_ch() == 'i' || peek_ch() == 'j') {
            next_ch();
            fi_is_imag = 1;
        }
        t = make_tok(TOK_FNUM, line, col);
        t->fval = (double)val;
        t->suffix_float = 1;
        t->suffix_imaginary = fi_is_imag;
        t->raw = copy_spelling(start, lex_pos);
        return t;
    }

    /* parse integer suffix: u/U/l/L combinations (incl LL/ULL) */
    t = make_tok(TOK_NUM, line, col);
    t->val = val;
    t->is_hex_or_oct = is_hex_oct;
    while (peek_ch() == 'u' || peek_ch() == 'U'
        || peek_ch() == 'l' || peek_ch() == 'L') {
        c = next_ch();
        if (c == 'u' || c == 'U') {
            t->suffix_unsigned = 1;
        } else if (c == 'l' || c == 'L') {
            if (peek_ch() == 'l' || peek_ch() == 'L') {
                next_ch();
                t->suffix_long = 2;
            } else {
                t->suffix_long = 1;
            }
        }
    }
    /* GNU imaginary suffix: i or j (e.g. 1i, 0j) */
    if (peek_ch() == 'i' || peek_ch() == 'j') {
        next_ch();
        /* Convert to float token with imaginary flag */
        t->kind = TOK_FNUM;
        t->fval = (double)t->val;
        t->val = 0;
        t->suffix_imaginary = 1;
    }
    t->raw = copy_spelling(start, lex_pos);
    return t;
}

/* forward declarations */
static struct tok *read_char_lit(const char *start, int wide);
static struct tok *read_string(const char *start);

/* ---- read identifier/keyword ---- */

static struct tok *read_ident(void)
{
    struct tok *t;
    int line, col;
    const char *start;
    char buf[256];
    int len;
    int i;
    int kwlen;

    line = lex_line;
    col = lex_col;
    start = lex_pos;
    len = 0;

    /* buffer identifier chars -- handles backslash-newline splicing
     * which may insert extra bytes in the raw source pointer range */
    while (is_alnum(peek_ch())) {
        if (len < (int)sizeof(buf) - 1) {
            buf[len++] = (char)next_ch();
        } else {
            next_ch();
        }
    }
    buf[len] = '\0';

    /* wide/Unicode character literal prefixes */
    if ((len == 1 && (buf[0] == 'L' || buf[0] == 'U' || buf[0] == 'u') &&
         peek_ch() == '\'') ||
        (len == 2 && buf[0] == 'u' && buf[1] == '8' &&
         peek_ch() == '\'')) {
        return read_char_lit(start, 1);
    }
    /* wide/Unicode string literal prefixes */
    if ((len == 1 && (buf[0] == 'L' || buf[0] == 'U' || buf[0] == 'u') &&
         peek_ch() == '"') ||
        (len == 2 && buf[0] == 'u' && buf[1] == '8' &&
         peek_ch() == '"')) {
        return read_string(start);
    }

    /* check C89 keyword table */
    for (i = 0; keywords[i].name != NULL; i++) {
        kwlen = (int)strlen(keywords[i].name);
        if (kwlen == len
            && strncmp(buf, keywords[i].name, len) == 0) {
            t = make_tok(keywords[i].kind, line, col);
            t->str = str_dup(lex_arena, buf, len);
            t->len = len;
            t->raw = t->str;
            return t;
        }
    }

    /* check C99/C11/C23 extended keywords */
    for (i = 0; ext_keywords[i].name != NULL; i++) {
        int ok;

        if (ext_keywords[i].feat == 0 && ext_keywords[i].feat2 == 0) {
            ok = 1;
        } else {
            ok = 0;
            if (ext_keywords[i].feat != 0) {
                ok = cc_has_feat(ext_keywords[i].feat);
            }
            if (ext_keywords[i].feat2 != 0) {
                ok = ok || cc_has_feat2(ext_keywords[i].feat2);
            }
        }
        if (!ok) {
            continue;
        }

        kwlen = (int)strlen(ext_keywords[i].name);
        if (kwlen == len
            && strncmp(buf, ext_keywords[i].name, len) == 0) {
            t = make_tok(ext_keywords[i].kind, line, col);
            t->str = str_dup(lex_arena, buf, len);
            t->len = len;
            t->raw = t->str;
            return t;
        }
    }

    t = make_tok(TOK_IDENT, line, col);
    t->str = str_dup(lex_arena, buf, len);
    t->len = len;
    t->raw = t->str;
    return t;
}

/* ---- read char literal ---- */

static unsigned long read_utf8_codepoint(void)
{
    unsigned long cp;
    unsigned char c0;
    unsigned char c1;
    unsigned char c2;
    unsigned char c3;

    c0 = (unsigned char)next_ch();
    if (c0 < 0x80) {
        return (unsigned long)c0;
    }

    if ((c0 & 0xE0) == 0xC0) {
        c1 = (unsigned char)peek_ch();
        if ((c1 & 0xC0) == 0x80) {
            next_ch();
            cp = ((unsigned long)(c0 & 0x1F) << 6)
               | (unsigned long)(c1 & 0x3F);
            return cp;
        }
    } else if ((c0 & 0xF0) == 0xE0) {
        c1 = (unsigned char)peek_ch();
        if ((c1 & 0xC0) == 0x80) {
            next_ch();
            c2 = (unsigned char)peek_ch();
            if ((c2 & 0xC0) == 0x80) {
                next_ch();
                cp = ((unsigned long)(c0 & 0x0F) << 12)
                   | ((unsigned long)(c1 & 0x3F) << 6)
                   | (unsigned long)(c2 & 0x3F);
                return cp;
            }
        }
    } else if ((c0 & 0xF8) == 0xF0) {
        c1 = (unsigned char)peek_ch();
        if ((c1 & 0xC0) == 0x80) {
            next_ch();
            c2 = (unsigned char)peek_ch();
            if ((c2 & 0xC0) == 0x80) {
                next_ch();
                c3 = (unsigned char)peek_ch();
                if ((c3 & 0xC0) == 0x80) {
                    next_ch();
                    cp = ((unsigned long)(c0 & 0x07) << 18)
                       | ((unsigned long)(c1 & 0x3F) << 12)
                       | ((unsigned long)(c2 & 0x3F) << 6)
                       | (unsigned long)(c3 & 0x3F);
                    return cp;
                }
            }
        }
    }

    return (unsigned long)c0;
}

static struct tok *read_char_lit(const char *start, int wide)
{
    struct tok *t;
    int line, col;
    int val;

    line = lex_line;
    col = lex_col;
    next_ch(); /* consume opening ' */

    if (peek_ch() == '\\') {
        next_ch(); /* consume backslash */
        val = read_escape();
    } else if (wide) {
        val = (int)read_utf8_codepoint();
    } else {
        val = (unsigned char)next_ch();
    }

    if (wide) {
        while (peek_ch() != '\'' && peek_ch() != '\0' && peek_ch() != '\n') {
            next_ch();
        }
    } else {
        /* multi-byte characters (e.g. UTF-8): read remaining bytes */
        while (peek_ch() != '\'' && peek_ch() != '\0' && peek_ch() != '\n') {
            if (peek_ch() == '\\') {
                next_ch();
                val = (val << 8) | (read_escape() & 0xFF);
            } else {
                val = (val << 8) | ((unsigned char)next_ch());
            }
        }
    }

    if (peek_ch() != '\'') {
        err_at(lex_filename, line, col, "unterminated character literal");
    } else {
        next_ch(); /* consume closing ' */
    }

    t = make_tok(TOK_CHAR_LIT, line, col);
    t->val = val;
    t->raw = copy_spelling(start, lex_pos);
    return t;
}

/* ---- read string literal ---- */

static struct tok *read_string(const char *start)
{
    struct tok *t;
    int line, col;
    char buf[4096];
    int len;
    int c;

    line = lex_line;
    col = lex_col;
    next_ch(); /* consume opening " */
    len = 0;

    while (peek_ch() != '"') {
        if (peek_ch() == '\0' || peek_ch() == '\n') {
            err_at(lex_filename, line, col, "unterminated string literal");
            break;
        }
        if (peek_ch() == '\\') {
            next_ch(); /* consume backslash */
            c = read_escape();
        } else {
            c = (unsigned char)next_ch();
        }
        if (len < (int)sizeof(buf) - 1) {
            buf[len++] = (char)c;
        }
    }

    if (peek_ch() == '"') {
        next_ch(); /* consume closing " */
    }

    buf[len] = '\0';
    t = make_tok(TOK_STR, line, col);
    t->str = str_dup(lex_arena, buf, len);
    t->len = len;
    t->raw = copy_spelling(start, lex_pos);
    return t;
}

/*
 * lex_keyword_kind - look up a string in the keyword tables.
 * Returns the keyword's tok_kind, or TOK_IDENT if not a keyword.
 */
enum tok_kind lex_keyword_kind(const char *name, int len)
{
    int i;
    int kwlen;

    /* check C89 keyword table */
    for (i = 0; keywords[i].name != NULL; i++) {
        kwlen = (int)strlen(keywords[i].name);
        if (kwlen == len && strncmp(name, keywords[i].name, (size_t)len) == 0) {
            return keywords[i].kind;
        }
    }

    /* check extended keywords (GCC alternates, C99/C11/C23) */
    for (i = 0; ext_keywords[i].name != NULL; i++) {
        int ok;
        if (ext_keywords[i].feat == 0 && ext_keywords[i].feat2 == 0) {
            ok = 1;
        } else {
            ok = 0;
            if (ext_keywords[i].feat != 0) {
                ok = cc_has_feat(ext_keywords[i].feat);
            }
            if (ext_keywords[i].feat2 != 0) {
                ok = ok || cc_has_feat2(ext_keywords[i].feat2);
            }
        }
        if (!ok) {
            continue;
        }
        kwlen = (int)strlen(ext_keywords[i].name);
        if (kwlen == len
            && strncmp(name, ext_keywords[i].name, (size_t)len) == 0) {
            return ext_keywords[i].kind;
        }
    }

    return TOK_IDENT;
}

/* ---- public interface ---- */

void lex_init(const char *src, const char *filename, struct arena *a)
{
    lex_src = src;
    lex_pos = src;
    lex_filename = filename;
    lex_line = 1;
    lex_col = 1;
    lex_has_ahead = 0;
    lex_arena = a;
}

/* return the current source position (for preprocessor use) */
const char *lex_get_pos(void)
{
    return lex_pos;
}

void lex_set_pos(const char *pos, int line, int col)
{
    lex_pos = pos;
    lex_line = line;
    lex_col = col;
    lex_has_ahead = 0;
}

int lex_get_line(void)
{
    return lex_line;
}

int lex_get_col(void)
{
    return lex_col;
}

const char *lex_get_filename(void)
{
    return lex_filename;
}

void lex_set_filename(const char *fn)
{
    lex_filename = fn;
}

struct tok *lex_next(void)
{
    struct tok *t;
    int line, col;
    char c;

    if (lex_has_ahead) {
        lex_has_ahead = 0;
        t = (struct tok *)arena_alloc(lex_arena, sizeof(struct tok));
        *t = lex_ahead;
        return t;
    }

retry:
    skip_whitespace();

    line = lex_line;
    col = lex_col;

    c = peek_ch();

    if (c == '\0') {
        return make_tok(TOK_EOF, line, col);
    }

    /* number literal */
    if (is_digit(c)) {
        return read_number();
    }

    /* identifier or keyword */
    if (is_alpha(c)) {
        return read_ident();
    }

    /* character literal */
    if (c == '\'') {
        return read_char_lit(lex_pos, 0);
    }

    /* string literal */
    if (c == '"') {
        return read_string(lex_pos);
    }

    /* operators and punctuation */
    next_ch();

    switch (c) {
    case '+':
        if (peek_ch() == '+') { next_ch(); return make_tok(TOK_INC, line, col); }
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_PLUS_EQ, line, col); }
        return make_tok(TOK_PLUS, line, col);
    case '-':
        if (peek_ch() == '-') { next_ch(); return make_tok(TOK_DEC, line, col); }
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_MINUS_EQ, line, col); }
        if (peek_ch() == '>') { next_ch(); return make_tok(TOK_ARROW, line, col); }
        return make_tok(TOK_MINUS, line, col);
    case '*':
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_STAR_EQ, line, col); }
        return make_tok(TOK_STAR, line, col);
    case '/':
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_SLASH_EQ, line, col); }
        return make_tok(TOK_SLASH, line, col);
    case '%':
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_PERCENT_EQ, line, col); }
        return make_tok(TOK_PERCENT, line, col);
    case '&':
        if (peek_ch() == '&') { next_ch(); return make_tok(TOK_AND, line, col); }
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_AMP_EQ, line, col); }
        return make_tok(TOK_AMP, line, col);
    case '|':
        if (peek_ch() == '|') { next_ch(); return make_tok(TOK_OR, line, col); }
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_PIPE_EQ, line, col); }
        return make_tok(TOK_PIPE, line, col);
    case '^':
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_CARET_EQ, line, col); }
        return make_tok(TOK_CARET, line, col);
    case '~':
        return make_tok(TOK_TILDE, line, col);
    case '!':
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_NE, line, col); }
        return make_tok(TOK_NOT, line, col);
    case '=':
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_EQ, line, col); }
        return make_tok(TOK_ASSIGN, line, col);
    case '<':
        if (peek_ch() == '<') {
            next_ch();
            if (peek_ch() == '=') { next_ch(); return make_tok(TOK_LSHIFT_EQ, line, col); }
            return make_tok(TOK_LSHIFT, line, col);
        }
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_LE, line, col); }
        return make_tok(TOK_LT, line, col);
    case '>':
        if (peek_ch() == '>') {
            next_ch();
            if (peek_ch() == '=') { next_ch(); return make_tok(TOK_RSHIFT_EQ, line, col); }
            return make_tok(TOK_RSHIFT, line, col);
        }
        if (peek_ch() == '=') { next_ch(); return make_tok(TOK_GE, line, col); }
        return make_tok(TOK_GT, line, col);
    case ';':  return make_tok(TOK_SEMI, line, col);
    case ',':  return make_tok(TOK_COMMA, line, col);
    case '.':
        {
            const char *start;

            start = lex_pos - 1;
            if (peek_ch() == '.' && peek_ch2() == '.') {
                next_ch();
                next_ch();
                return make_tok(TOK_ELLIPSIS, line, col);
            }
            /* .5 style float literal */
            if (is_digit(peek_ch())) {
                double fv;

                fv = parse_frac_digits();
                if (peek_ch() == 'e' || peek_ch() == 'E') {
                    int es, en;
                    double ev;

                    next_ch();
                    es = 1;
                    en = 0;
                    if (peek_ch() == '+') {
                        next_ch();
                    } else if (peek_ch() == '-') {
                        next_ch();
                        es = -1;
                    }
                    while (is_digit(peek_ch())) {
                        en = en * 10 + (next_ch() - '0');
                    }
                    ev = 1.0;
                    while (en > 0) { ev *= 10.0; en--; }
                    if (es < 0) fv /= ev; else fv *= ev;
                }
                /* GNU imaginary suffix before type suffix */
                {
                    int dot_is_imag;
                    int dot_is_float;
                    int dot_is_long;
                    dot_is_imag = 0;
                    dot_is_float = 0;
                    dot_is_long = 0;
                    if (peek_ch() == 'i' || peek_ch() == 'j') {
                        next_ch();
                        dot_is_imag = 1;
                    }
                    if (peek_ch() == 'f' || peek_ch() == 'F') {
                        dot_is_float = 1;
                        next_ch();
                    } else if (peek_ch() == 'l' || peek_ch() == 'L') {
                        dot_is_long = 1;
                        next_ch();
                    } else if (peek_ch() == 'd' || peek_ch() == 'D') {
                        /* GCC decimal float suffix: DD, DF, DL */
                        next_ch();
                        if (peek_ch() == 'd' || peek_ch() == 'D' ||
                            peek_ch() == 'f' || peek_ch() == 'F' ||
                            peek_ch() == 'l' || peek_ch() == 'L') {
                            next_ch();
                        }
                    }
                    /* GNU imaginary suffix after type suffix */
                    if (peek_ch() == 'i' || peek_ch() == 'j') {
                        next_ch();
                        dot_is_imag = 1;
                    }
                    t = make_tok(TOK_FNUM, line, col);
                    t->fval = fv;
                    t->suffix_float = dot_is_float;
                    t->suffix_long = dot_is_long;
                    t->suffix_imaginary = dot_is_imag;
                    t->raw = copy_spelling(start, lex_pos);
                    return t;
                }
            }
            return make_tok(TOK_DOT, line, col);
        }
    case '(':  return make_tok(TOK_LPAREN, line, col);
    case ')':  return make_tok(TOK_RPAREN, line, col);
    case '{':  return make_tok(TOK_LBRACE, line, col);
    case '}':  return make_tok(TOK_RBRACE, line, col);
    case '[':
        /* C23 [[ attribute syntax */
        if (peek_ch() == '[' && cc_has_feat(FEAT_ATTR_SYNTAX)) {
            next_ch();
            return make_tok(TOK_ATTR_OPEN, line, col);
        }
        return make_tok(TOK_LBRACKET, line, col);
    case ']':
        /* C23 ]] attribute close */
        if (peek_ch() == ']' && cc_has_feat(FEAT_ATTR_SYNTAX)) {
            next_ch();
            return make_tok(TOK_ATTR_CLOSE, line, col);
        }
        return make_tok(TOK_RBRACKET, line, col);
    case '?':  return make_tok(TOK_QUESTION, line, col);
    case ':':  return make_tok(TOK_COLON, line, col);
    case '#':
        if (peek_ch() == '#') {
            next_ch();
            return make_tok(TOK_PASTE, line, col);
        }
        return make_tok(TOK_HASH, line, col);
    case '\\':
        /* backslash outside of strings/chars: used in GAS .macro syntax
         * inside preprocessor-skipped blocks. Emit an ident-like token
         * so the preprocessor can skip it in inactive conditional blocks. */
        {
            char buf[2];
            struct tok *bt;
            buf[0] = '\\';
            buf[1] = '\0';
            bt = make_tok(TOK_IDENT, line, col);
            bt->str = str_dup(lex_arena, buf, 1);
            bt->len = 1;
            return bt;
        }
    default:
        /* skip unexpected characters silently; they may appear in
         * preprocessor-skipped blocks (e.g. GAS assembly syntax).
         * The character was already consumed by next_ch() above. */
        goto retry;
    }
}

struct tok *lex_peek(void)
{
    struct tok *t;

    if (lex_has_ahead) {
        t = (struct tok *)arena_alloc(lex_arena, sizeof(struct tok));
        *t = lex_ahead;
        return t;
    }

    t = lex_next();
    lex_ahead = *t;
    lex_has_ahead = 1;
    return t;
}
