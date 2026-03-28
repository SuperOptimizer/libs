/*
 * script.c - Linker script parser and executor for the free linker
 * Parses GNU ld-compatible linker scripts and drives section layout.
 * Supports the subset used by the Linux kernel vmlinux.lds:
 *   SECTIONS, ENTRY, PROVIDE, PROVIDE_HIDDEN, ASSERT,
 *   OUTPUT_ARCH, OUTPUT_FORMAT, /DISCARD/, KEEP, SORT,
 *   ALIGN, SIZEOF, ADDR, AT, BYTE/SHORT/LONG/QUAD,
 *   location counter, symbol definitions, wildcard matching.
 * Pure C89.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ld_internal.h"

/* ---- token types for the script lexer ---- */
enum stok_kind {
    ST_EOF, ST_IDENT, ST_NUM, ST_STR,
    ST_LBRACE, ST_RBRACE, ST_LPAREN, ST_RPAREN,
    ST_SEMI, ST_COLON, ST_COMMA, ST_DOT,
    ST_ASSIGN, ST_PLUS_EQ, ST_MINUS_EQ, ST_STAR_EQ,
    ST_SLASH_EQ, ST_AMP_EQ, ST_PIPE_EQ,
    ST_LSHIFT_EQ, ST_RSHIFT_EQ,
    ST_PLUS, ST_MINUS, ST_STAR, ST_SLASH, ST_PERCENT,
    ST_AMP, ST_PIPE, ST_TILDE, ST_BANG,
    ST_LSHIFT, ST_RSHIFT,
    ST_EQ, ST_NE, ST_LT, ST_GT, ST_LE, ST_GE,
    ST_AND, ST_OR, ST_QUESTION
};

struct stok {
    enum stok_kind kind;
    u64 num;
    char str[256];
};

/* ---- data structures ---- */

/* sort modes for section matching */
#define SORT_NONE           0
#define SORT_BY_NAME        1
#define SORT_BY_ALIGN       2
#define SORT_BY_INIT_PRI    3

struct script_rule {
    char *file_pattern;
    char **section_patterns;
    int npatterns;
    int pat_cap;
    int keep;
    int sort;           /* SORT_NONE, SORT_BY_NAME, SORT_BY_ALIGN, etc */
};

struct script_data_cmd {
    int width;          /* 1=BYTE, 2=SHORT, 4=LONG, 8=QUAD */
    u64 value;
};

enum sym_kind {
    SYM_ASSIGN,         /* symbol = expr */
    SYM_PROVIDE,        /* PROVIDE(symbol = expr) */
    SYM_PROVIDE_HIDDEN  /* PROVIDE_HIDDEN(symbol = expr) */
};

struct script_sym {
    enum sym_kind kind;
    char *name;
    int is_dot;         /* 1 if this is ". = expr" (location counter) */
    u64 value;          /* evaluated later */
    int value_is_dot;   /* RHS is just "." */
    u64 addend;         /* for ". + N" expressions */
    u64 align;          /* for ALIGN(N) expressions */
};

struct script_section {
    char *name;
    struct script_rule *rules;
    int nrules;
    int rule_cap;
    struct script_sym *syms;
    int nsyms;
    int sym_cap;
    struct script_data_cmd *data_cmds;
    int ndata;
    int data_cap;
    u64 address;        /* explicit address, 0 if auto */
    int has_address;
    u64 align;
    u64 load_addr;      /* AT(addr) */
    int has_load_addr;
    int discard;        /* /DISCARD/ */
    int noload;         /* (NOLOAD) flag */
    int sec_flags;      /* parsed section type flags */
    /* PHDRS assignment: names from :phdr1 :phdr2 after section body */
    char **phdr_names;
    int nphdr_names;
    int phdr_name_cap;
};

/* ---- PHDRS block definitions ---- */
struct script_phdr {
    char *name;
    u32 type;           /* PT_LOAD, PT_NOTE, PT_PHDR, etc */
    u32 flags;          /* PF_R|PF_W|PF_X, 0 = derive from sections */
    int has_flags;
    int filehdr;        /* FILEHDR keyword present */
    int phdrs;          /* PHDRS keyword present */
    u64 at_addr;        /* AT(addr) */
    int has_at;
};

struct script_assert {
    char *msg;
    u64 expr_value;
};

enum script_cmd_kind {
    CMD_SECTIONS,
    CMD_ENTRY,
    CMD_PROVIDE,
    CMD_PROVIDE_HIDDEN,
    CMD_ASSERT,
    CMD_OUTPUT_ARCH,
    CMD_OUTPUT_FORMAT,
    CMD_SYMBOL,
    CMD_PHDRS
};

struct script_cmd {
    enum script_cmd_kind kind;
    char *str_arg;              /* ENTRY symbol, OUTPUT_ARCH, etc. */
    struct script_section *sections;
    int nsections;
    int sec_cap;
    struct script_sym *sym;     /* top-level symbol assignment */
    /* PHDRS command data */
    struct script_phdr *phdrs;
    int nphdrs;
    int phdr_cap;
};

struct ld_script {
    struct script_cmd *cmds;
    int ncmds;
    int cmd_cap;
    char *entry_symbol;
    /* cached PHDRS definitions (pointer into cmd) */
    struct script_phdr *phdrs;
    int nphdrs;
    /* raw script text for deferred re-evaluation */
    char *text;
    unsigned long text_len;
};

/* ---- lexer state ---- */
static const char *s_pos;
static const char *s_end;
static int s_line;

static void skip_whitespace(void)
{
    while (s_pos < s_end) {
        if (*s_pos == ' ' || *s_pos == '\t' || *s_pos == '\r') {
            s_pos++;
        } else if (*s_pos == '\n') {
            s_pos++;
            s_line++;
        } else if (s_pos + 1 < s_end && s_pos[0] == '/' && s_pos[1] == '*') {
            s_pos += 2;
            while (s_pos + 1 < s_end) {
                if (*s_pos == '\n') s_line++;
                if (s_pos[0] == '*' && s_pos[1] == '/') {
                    s_pos += 2;
                    break;
                }
                s_pos++;
            }
        } else {
            break;
        }
    }
}

static int is_ident_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '.' ||
           c == '-' || c == '*' || c == '?' || c == '[' || c == ']' ||
           c == '$';
}

static int is_ident_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '.' || c == '!';
}

static void next_token(struct stok *t)
{
    const char *start;
    int len;

    skip_whitespace();

    if (s_pos >= s_end) {
        t->kind = ST_EOF;
        t->str[0] = '\0';
        return;
    }

    switch (*s_pos) {
    case '{': t->kind = ST_LBRACE;  s_pos++; return;
    case '}': t->kind = ST_RBRACE;  s_pos++; return;
    case '(': t->kind = ST_LPAREN;  s_pos++; return;
    case ')': t->kind = ST_RPAREN;  s_pos++; return;
    case ';': t->kind = ST_SEMI;    s_pos++; return;
    case ':': t->kind = ST_COLON;   s_pos++; return;
    case ',': t->kind = ST_COMMA;   s_pos++; return;
    case '~': t->kind = ST_TILDE;   s_pos++; return;
    case '?': t->kind = ST_QUESTION; s_pos++; return;
    case '%': t->kind = ST_PERCENT; s_pos++; return;
    case '/':
        /* /DISCARD/ and similar: '/' followed by alpha is an ident */
        if (s_pos + 1 < s_end &&
            ((s_pos[1] >= 'A' && s_pos[1] <= 'Z') ||
             (s_pos[1] >= 'a' && s_pos[1] <= 'z') ||
             s_pos[1] == '_' || s_pos[1] == '!')) {
            start = s_pos;
            s_pos++;
            while (s_pos < s_end && is_ident_char(*s_pos)) s_pos++;
            if (s_pos < s_end && *s_pos == '/') s_pos++;
            len = (int)(s_pos - start);
            if (len > 255) len = 255;
            memcpy(t->str, start, (unsigned long)len);
            t->str[len] = '\0';
            t->kind = ST_IDENT;
            return;
        }
        s_pos++;
        if (s_pos < s_end && *s_pos == '=') {
            t->kind = ST_SLASH_EQ; s_pos++;
        } else {
            t->kind = ST_SLASH;
        }
        return;
    case '+':
        s_pos++;
        if (s_pos < s_end && *s_pos == '=') { t->kind = ST_PLUS_EQ; s_pos++; }
        else { t->kind = ST_PLUS; }
        return;
    case '-':
        s_pos++;
        if (s_pos < s_end && *s_pos == '=') { t->kind = ST_MINUS_EQ; s_pos++; }
        else { t->kind = ST_MINUS; }
        return;
    case '&':
        s_pos++;
        if (s_pos < s_end && *s_pos == '&') { t->kind = ST_AND; s_pos++; }
        else if (s_pos < s_end && *s_pos == '=') { t->kind = ST_AMP_EQ; s_pos++; }
        else { t->kind = ST_AMP; }
        return;
    case '|':
        s_pos++;
        if (s_pos < s_end && *s_pos == '|') { t->kind = ST_OR; s_pos++; }
        else if (s_pos < s_end && *s_pos == '=') { t->kind = ST_PIPE_EQ; s_pos++; }
        else { t->kind = ST_PIPE; }
        return;
    case '!':
        s_pos++;
        if (s_pos < s_end && *s_pos == '=') { t->kind = ST_NE; s_pos++; }
        else { t->kind = ST_BANG; }
        return;
    case '=':
        s_pos++;
        if (s_pos < s_end && *s_pos == '=') { t->kind = ST_EQ; s_pos++; }
        else { t->kind = ST_ASSIGN; }
        return;
    case '<':
        s_pos++;
        if (s_pos < s_end && *s_pos == '<') {
            s_pos++;
            if (s_pos < s_end && *s_pos == '=') { t->kind = ST_LSHIFT_EQ; s_pos++; }
            else { t->kind = ST_LSHIFT; }
        } else if (s_pos < s_end && *s_pos == '=') {
            t->kind = ST_LE; s_pos++;
        } else {
            t->kind = ST_LT;
        }
        return;
    case '>':
        s_pos++;
        if (s_pos < s_end && *s_pos == '>') {
            s_pos++;
            if (s_pos < s_end && *s_pos == '=') { t->kind = ST_RSHIFT_EQ; s_pos++; }
            else { t->kind = ST_RSHIFT; }
        } else if (s_pos < s_end && *s_pos == '=') {
            t->kind = ST_GE; s_pos++;
        } else {
            t->kind = ST_GT;
        }
        return;
    case '"':
        s_pos++;
        start = s_pos;
        while (s_pos < s_end && *s_pos != '"') {
            if (*s_pos == '\\' && s_pos + 1 < s_end) s_pos++;
            s_pos++;
        }
        len = (int)(s_pos - start);
        if (len > 255) len = 255;
        memcpy(t->str, start, (unsigned long)len);
        t->str[len] = '\0';
        if (s_pos < s_end) s_pos++;
        t->kind = ST_STR;
        return;
    default:
        break;
    }

    /* number */
    if (*s_pos >= '0' && *s_pos <= '9') {
        u64 val = 0;
        if (s_pos + 1 < s_end && s_pos[0] == '0' &&
            (s_pos[1] == 'x' || s_pos[1] == 'X')) {
            s_pos += 2;
            while (s_pos < s_end) {
                char c = *s_pos;
                if (c >= '0' && c <= '9') { val = val * 16 + (u64)(c - '0'); }
                else if (c >= 'a' && c <= 'f') { val = val * 16 + (u64)(c - 'a' + 10); }
                else if (c >= 'A' && c <= 'F') { val = val * 16 + (u64)(c - 'A' + 10); }
                else break;
                s_pos++;
            }
        } else {
            while (s_pos < s_end && *s_pos >= '0' && *s_pos <= '9') {
                val = val * 10 + (u64)(*s_pos - '0');
                s_pos++;
            }
        }
        /* skip K/M suffix */
        if (s_pos < s_end) {
            if (*s_pos == 'K' || *s_pos == 'k') { val *= 1024; s_pos++; }
            else if (*s_pos == 'M' || *s_pos == 'm') { val *= 1024 * 1024; s_pos++; }
        }
        t->kind = ST_NUM;
        t->num = val;
        return;
    }

    /* identifier or keyword - includes wildcards, paths, /DISCARD/ */
    if (is_ident_start(*s_pos)) {
        start = s_pos;
        /* /DISCARD/ is special */
        if (*s_pos == '/') {
            s_pos++;
            while (s_pos < s_end && is_ident_char(*s_pos)) s_pos++;
            if (s_pos < s_end && *s_pos == '/') s_pos++;
        } else {
            while (s_pos < s_end && is_ident_char(*s_pos)) s_pos++;
        }
        len = (int)(s_pos - start);
        if (len > 255) len = 255;
        memcpy(t->str, start, (unsigned long)len);
        t->str[len] = '\0';
        t->kind = ST_IDENT;
        return;
    }

    /* single character fallback: treat as operator */
    if (*s_pos == '*') {
        t->kind = ST_STAR;
        s_pos++;
        return;
    }

    fprintf(stderr, "ld: script:%d: unexpected character '%c'\n",
            s_line, *s_pos);
    s_pos++;
    next_token(t);
}

static struct stok s_cur;
static struct stok s_peek;
static int s_has_peek;

static void lex_init(const char *data, unsigned long size)
{
    s_pos = data;
    s_end = data + size;
    s_line = 1;
    s_has_peek = 0;
    next_token(&s_cur);
}

static void advance(void)
{
    if (s_has_peek) {
        s_cur = s_peek;
        s_has_peek = 0;
    } else {
        next_token(&s_cur);
    }
}

static struct stok *peek(void)
{
    if (!s_has_peek) {
        next_token(&s_peek);
        s_has_peek = 1;
    }
    return &s_peek;
}

static void expect(enum stok_kind k, const char *what)
{
    if (s_cur.kind != k) {
        fprintf(stderr, "ld: script:%d: expected %s\n", s_line, what);
        exit(1);
    }
    advance();
}

static int match(enum stok_kind k)
{
    if (s_cur.kind == k) {
        advance();
        return 1;
    }
    return 0;
}

static char *dup_str(const char *s)
{
    unsigned long len = strlen(s);
    char *p = (char *)malloc(len + 1);
    if (!p) { fprintf(stderr, "ld: out of memory\n"); exit(1); }
    memcpy(p, s, len + 1);
    return p;
}

/* ---- wildcard matching ---- */

int script_glob_match(const char *pat, const char *str)
{
    while (*pat && *str) {
        if (*pat == '*') {
            pat++;
            if (*pat == '\0') return 1;
            while (*str) {
                if (script_glob_match(pat, str)) return 1;
                str++;
            }
            return 0;
        }
        if (*pat == '?') {
            pat++;
            str++;
            continue;
        }
        if (*pat == '[') {
            int inv = 0;
            int found = 0;
            pat++;
            if (*pat == '!' || *pat == '^') { inv = 1; pat++; }
            while (*pat && *pat != ']') {
                char lo = *pat++;
                if (*pat == '-' && pat[1] && pat[1] != ']') {
                    char hi;
                    pat++;
                    hi = *pat++;
                    if (*str >= lo && *str <= hi) found = 1;
                } else {
                    if (*str == lo) found = 1;
                }
            }
            if (*pat == ']') pat++;
            if (found == inv) return 0;
            str++;
            continue;
        }
        if (*pat != *str) return 0;
        pat++;
        str++;
    }
    while (*pat == '*') pat++;
    return (*pat == '\0' && *str == '\0');
}

/* ---- expression parser ---- */

/*
 * Expression grammar (simplified for linker scripts):
 *   expr     = ternary
 *   ternary  = or_expr [ '?' expr ':' expr ]
 *   or_expr  = and_expr { '||' and_expr }
 *   and_expr = bitor    { '&&' bitor }
 *   bitor    = bitxor   { '|' bitxor }  (not used, folded)
 *   shift    = add      { ('<<'|'>>') add }
 *   add      = mul      { ('+'|'-') mul }
 *   mul      = unary    { ('*'|'/'|'%') unary }
 *   unary    = '-' unary | '~' unary | '!' unary | primary
 *   primary  = NUM | '.' | IDENT | func '(' args ')' | '(' expr ')'
 *   func     = ALIGN | SIZEOF | ADDR | DEFINED | ...
 */

/* forward declarations for the section layout context */
struct exec_ctx {
    u64 dot;
    struct merged_section *msecs;
    int nmsecs;
    int num_phdrs;      /* number of PHDRS for SIZEOF_HEADERS */
};

static struct exec_ctx *s_ectx;

static u64 parse_expr(void);

static u64 parse_primary(void)
{
    u64 val;

    if (s_cur.kind == ST_NUM) {
        val = s_cur.num;
        advance();
        return val;
    }

    if (s_cur.kind == ST_DOT ||
        (s_cur.kind == ST_IDENT && s_cur.str[0] == '.' &&
         s_cur.str[1] == '\0')) {
        advance();
        return s_ectx ? s_ectx->dot : 0;
    }

    if (s_cur.kind == ST_LPAREN) {
        advance();
        val = parse_expr();
        expect(ST_RPAREN, "')'");
        return val;
    }

    if (s_cur.kind == ST_IDENT) {
        /* built-in functions */
        if (strcmp(s_cur.str, "ALIGN") == 0) {
            u64 a, b;
            advance();
            expect(ST_LPAREN, "'('");
            a = parse_expr();
            if (s_cur.kind == ST_COMMA) {
                advance();
                b = parse_expr();
                /* ALIGN(expr, align) */
                val = (a + b - 1) & ~(b - 1);
            } else {
                /* ALIGN(align) - align the location counter */
                if (s_ectx) {
                    val = (s_ectx->dot + a - 1) & ~(a - 1);
                } else {
                    val = a;
                }
            }
            expect(ST_RPAREN, "')'");
            return val;
        }
        if (strcmp(s_cur.str, "SIZEOF") == 0) {
            advance();
            expect(ST_LPAREN, "'('");
            if (s_cur.kind == ST_IDENT || s_cur.kind == ST_DOT) {
                /* look up section size */
                val = 0;
                if (s_ectx) {
                    int si;
                    for (si = 0; si < s_ectx->nmsecs; si++) {
                        if (strcmp(s_ectx->msecs[si].name, s_cur.str) == 0) {
                            val = s_ectx->msecs[si].size;
                            break;
                        }
                    }
                }
                advance();
            } else {
                val = 0;
                advance();
            }
            expect(ST_RPAREN, "')'");
            return val;
        }
        if (strcmp(s_cur.str, "ADDR") == 0) {
            advance();
            expect(ST_LPAREN, "'('");
            val = 0;
            if (s_cur.kind == ST_IDENT || s_cur.kind == ST_DOT) {
                if (s_ectx) {
                    int si;
                    for (si = 0; si < s_ectx->nmsecs; si++) {
                        if (strcmp(s_ectx->msecs[si].name, s_cur.str) == 0) {
                            val = s_ectx->msecs[si].shdr.sh_addr;
                            break;
                        }
                    }
                }
                advance();
            } else {
                advance();
            }
            expect(ST_RPAREN, "')'");
            return val;
        }
        if (strcmp(s_cur.str, "DEFINED") == 0) {
            advance();
            expect(ST_LPAREN, "'('");
            /* always treat as defined=0 unless we have the symbol table */
            val = 0;
            if (s_cur.kind == ST_IDENT) advance();
            expect(ST_RPAREN, "')'");
            return val;
        }
        if (strcmp(s_cur.str, "ABSOLUTE") == 0) {
            advance();
            expect(ST_LPAREN, "'('");
            val = parse_expr();
            expect(ST_RPAREN, "')'");
            return val;
        }
        if (strcmp(s_cur.str, "SIZEOF_HEADERS") == 0) {
            int nph = 4; /* default estimate */
            advance();
            if (s_ectx && s_ectx->num_phdrs > 0) {
                nph = s_ectx->num_phdrs;
            }
            return sizeof(Elf64_Ehdr) + (u64)nph * sizeof(Elf64_Phdr);
        }
        if (strcmp(s_cur.str, "LOADADDR") == 0) {
            advance();
            expect(ST_LPAREN, "'('");
            val = 0;
            if (s_cur.kind == ST_IDENT || s_cur.kind == ST_DOT) {
                if (s_ectx) {
                    int si;
                    for (si = 0; si < s_ectx->nmsecs; si++) {
                        if (strcmp(s_ectx->msecs[si].name,
                                  s_cur.str) == 0) {
                            /* load address defaults to vaddr */
                            val = s_ectx->msecs[si].shdr.sh_addr;
                            break;
                        }
                    }
                }
                advance();
            } else {
                advance();
            }
            expect(ST_RPAREN, "')'");
            return val;
        }
        if (strcmp(s_cur.str, "MAX") == 0) {
            u64 a, b;
            advance();
            expect(ST_LPAREN, "'('");
            a = parse_expr();
            expect(ST_COMMA, "','");
            b = parse_expr();
            expect(ST_RPAREN, "')'");
            return a > b ? a : b;
        }
        if (strcmp(s_cur.str, "MIN") == 0) {
            u64 a, b;
            advance();
            expect(ST_LPAREN, "'('");
            a = parse_expr();
            expect(ST_COMMA, "','");
            b = parse_expr();
            expect(ST_RPAREN, "')'");
            return a < b ? a : b;
        }
        if (strcmp(s_cur.str, "DATA_SEGMENT_ALIGN") == 0 ||
            strcmp(s_cur.str, "DATA_SEGMENT_END") == 0 ||
            strcmp(s_cur.str, "DATA_SEGMENT_RELRO_END") == 0) {
            /* passthrough: consume func(expr, expr) */
            advance();
            expect(ST_LPAREN, "'('");
            val = parse_expr();
            if (s_cur.kind == ST_COMMA) {
                advance();
                (void)parse_expr();
            }
            expect(ST_RPAREN, "')'");
            return val;
        }
        if (strcmp(s_cur.str, "SEGMENT_START") == 0) {
            advance();
            expect(ST_LPAREN, "'('");
            /* skip segment name string */
            if (s_cur.kind == ST_STR || s_cur.kind == ST_IDENT) advance();
            expect(ST_COMMA, "','");
            val = parse_expr();
            expect(ST_RPAREN, "')'");
            return val;
        }
        if (strcmp(s_cur.str, "CONSTANT") == 0) {
            advance();
            expect(ST_LPAREN, "'('");
            val = 0;
            if (s_cur.kind == ST_IDENT) {
                if (strcmp(s_cur.str, "MAXPAGESIZE") == 0) val = 0x1000;
                else if (strcmp(s_cur.str, "COMMONPAGESIZE") == 0) val = 0x1000;
                advance();
            }
            expect(ST_RPAREN, "')'");
            return val;
        }

        /* unknown identifier - look up in symbol table */
        {
            u64 sym_addr;
            char sym_name[256];
            int slen = (int)strlen(s_cur.str);
            if (slen > 255) slen = 255;
            memcpy(sym_name, s_cur.str, (unsigned long)slen);
            sym_name[slen] = '\0';
            advance();
            if (find_entry_symbol_safe(sym_name, &sym_addr)) {
                return sym_addr;
            }
            return 0;
        }
    }

    /* fallback */
    advance();
    return 0;
}

static u64 parse_unary(void)
{
    if (s_cur.kind == ST_MINUS) {
        advance();
        return (u64)(-(i64)parse_unary());
    }
    if (s_cur.kind == ST_TILDE) {
        advance();
        return ~parse_unary();
    }
    if (s_cur.kind == ST_BANG) {
        advance();
        return parse_unary() == 0 ? 1 : 0;
    }
    return parse_primary();
}

static u64 parse_mul(void)
{
    u64 val = parse_unary();
    while (s_cur.kind == ST_STAR || s_cur.kind == ST_SLASH ||
           s_cur.kind == ST_PERCENT) {
        enum stok_kind op = s_cur.kind;
        u64 rhs;
        advance();
        rhs = parse_unary();
        if (op == ST_STAR) val *= rhs;
        else if (op == ST_SLASH) val = rhs ? val / rhs : 0;
        else val = rhs ? val % rhs : 0;
    }
    return val;
}

static u64 parse_add(void)
{
    u64 val = parse_mul();
    while (s_cur.kind == ST_PLUS || s_cur.kind == ST_MINUS) {
        enum stok_kind op = s_cur.kind;
        u64 rhs;
        advance();
        rhs = parse_mul();
        if (op == ST_PLUS) val += rhs;
        else val -= rhs;
    }
    return val;
}

static u64 parse_shift(void)
{
    u64 val = parse_add();
    while (s_cur.kind == ST_LSHIFT || s_cur.kind == ST_RSHIFT) {
        enum stok_kind op = s_cur.kind;
        u64 rhs;
        advance();
        rhs = parse_add();
        if (op == ST_LSHIFT) val <<= rhs;
        else val >>= rhs;
    }
    return val;
}

static u64 parse_rel(void)
{
    u64 val = parse_shift();
    while (s_cur.kind == ST_LT || s_cur.kind == ST_GT ||
           s_cur.kind == ST_LE || s_cur.kind == ST_GE) {
        enum stok_kind op = s_cur.kind;
        u64 rhs;
        advance();
        rhs = parse_shift();
        if (op == ST_LT) val = val < rhs ? 1 : 0;
        else if (op == ST_GT) val = val > rhs ? 1 : 0;
        else if (op == ST_LE) val = val <= rhs ? 1 : 0;
        else val = val >= rhs ? 1 : 0;
    }
    return val;
}

static u64 parse_eq(void)
{
    u64 val = parse_rel();
    while (s_cur.kind == ST_EQ || s_cur.kind == ST_NE) {
        enum stok_kind op = s_cur.kind;
        u64 rhs;
        advance();
        rhs = parse_rel();
        if (op == ST_EQ) val = (val == rhs) ? 1 : 0;
        else val = (val != rhs) ? 1 : 0;
    }
    return val;
}

static u64 parse_bitand(void)
{
    u64 val = parse_eq();
    while (s_cur.kind == ST_AMP) {
        advance();
        val &= parse_eq();
    }
    return val;
}

static u64 parse_bitor(void)
{
    u64 val = parse_bitand();
    while (s_cur.kind == ST_PIPE) {
        advance();
        val |= parse_bitand();
    }
    return val;
}

static u64 parse_logand(void)
{
    u64 val = parse_bitor();
    while (s_cur.kind == ST_AND) {
        u64 rhs;
        advance();
        rhs = parse_bitor();
        val = (val && rhs) ? 1 : 0;
    }
    return val;
}

static u64 parse_logor(void)
{
    u64 val = parse_logand();
    while (s_cur.kind == ST_OR) {
        u64 rhs;
        advance();
        rhs = parse_logand();
        val = (val || rhs) ? 1 : 0;
    }
    return val;
}

static u64 parse_expr(void)
{
    u64 val = parse_logor();
    if (s_cur.kind == ST_QUESTION) {
        u64 then_val, else_val;
        advance();
        then_val = parse_expr();
        expect(ST_COLON, "':'");
        else_val = parse_expr();
        val = val ? then_val : else_val;
    }
    return val;
}

/* ---- parser helpers ---- */

static void add_rule(struct script_section *sec, struct script_rule *r)
{
    if (sec->nrules >= sec->rule_cap) {
        sec->rule_cap = sec->rule_cap ? sec->rule_cap * 2 : 8;
        sec->rules = (struct script_rule *)realloc(sec->rules,
            (unsigned long)sec->rule_cap * sizeof(struct script_rule));
        if (!sec->rules) { fprintf(stderr, "ld: out of memory\n"); exit(1); }
    }
    sec->rules[sec->nrules++] = *r;
}

static void add_sym(struct script_section *sec, struct script_sym *sym)
{
    if (sec->nsyms >= sec->sym_cap) {
        sec->sym_cap = sec->sym_cap ? sec->sym_cap * 2 : 8;
        sec->syms = (struct script_sym *)realloc(sec->syms,
            (unsigned long)sec->sym_cap * sizeof(struct script_sym));
        if (!sec->syms) { fprintf(stderr, "ld: out of memory\n"); exit(1); }
    }
    sec->syms[sec->nsyms++] = *sym;
}

static void add_section(struct script_cmd *cmd, struct script_section *sec)
{
    if (cmd->nsections >= cmd->sec_cap) {
        cmd->sec_cap = cmd->sec_cap ? cmd->sec_cap * 2 : 16;
        cmd->sections = (struct script_section *)realloc(cmd->sections,
            (unsigned long)cmd->sec_cap * sizeof(struct script_section));
        if (!cmd->sections) {
            fprintf(stderr, "ld: out of memory\n"); exit(1);
        }
    }
    cmd->sections[cmd->nsections++] = *sec;
}

static void add_cmd(struct ld_script *sc, struct script_cmd *cmd)
{
    if (sc->ncmds >= sc->cmd_cap) {
        sc->cmd_cap = sc->cmd_cap ? sc->cmd_cap * 2 : 16;
        sc->cmds = (struct script_cmd *)realloc(sc->cmds,
            (unsigned long)sc->cmd_cap * sizeof(struct script_cmd));
        if (!sc->cmds) { fprintf(stderr, "ld: out of memory\n"); exit(1); }
    }
    sc->cmds[sc->ncmds++] = *cmd;
}

static void add_pattern(struct script_rule *r, const char *pat)
{
    if (r->npatterns >= r->pat_cap) {
        r->pat_cap = r->pat_cap ? r->pat_cap * 2 : 4;
        r->section_patterns = (char **)realloc(r->section_patterns,
            (unsigned long)r->pat_cap * sizeof(char *));
        if (!r->section_patterns) {
            fprintf(stderr, "ld: out of memory\n"); exit(1);
        }
    }
    r->section_patterns[r->npatterns++] = dup_str(pat);
}

/* ---- section content parser ---- */

/*
 * Parse input section rules inside { ... } of an output section.
 * Handles: *(.text .text.*)  KEEP(*(.init))  SORT(*)(.text)
 *          . = expr;  sym = expr;  PROVIDE(sym = expr);
 *          BYTE(expr) SHORT(expr) LONG(expr) QUAD(expr)
 */
static void parse_section_body(struct script_section *sec)
{
    expect(ST_LBRACE, "'{'");

    while (s_cur.kind != ST_RBRACE && s_cur.kind != ST_EOF) {
        /* location counter or symbol assignment: . = expr ; or . += expr ; */
        if ((s_cur.kind == ST_DOT ||
             (s_cur.kind == ST_IDENT && strcmp(s_cur.str, ".") == 0)) &&
            (peek()->kind == ST_ASSIGN ||
             peek()->kind == ST_PLUS_EQ ||
             peek()->kind == ST_MINUS_EQ)) {
            struct script_sym sym;
            int compound_op;
            memset(&sym, 0, sizeof(sym));
            sym.is_dot = 1;
            sym.name = dup_str(".");
            advance(); /* consume '.' */
            compound_op = s_cur.kind;
            advance(); /* consume '=', '+=', or '-=' */
            sym.value = parse_expr();
            sym.kind = SYM_ASSIGN;
            if (compound_op == ST_PLUS_EQ) {
                sym.value_is_dot = 1;
                sym.addend = sym.value;
                sym.value = 0;
            } else if (compound_op == ST_MINUS_EQ) {
                sym.value_is_dot = 1;
                sym.addend = (u64)(-(i64)sym.value);
                sym.value = 0;
            }
            add_sym(sec, &sym);
            match(ST_SEMI);
            continue;
        }

        /* PROVIDE( sym = expr ) or PROVIDE_HIDDEN( sym = expr ) */
        if (s_cur.kind == ST_IDENT &&
            (strcmp(s_cur.str, "PROVIDE") == 0 ||
             strcmp(s_cur.str, "PROVIDE_HIDDEN") == 0)) {
            struct script_sym sym;
            int hidden = (strcmp(s_cur.str, "PROVIDE_HIDDEN") == 0);
            memset(&sym, 0, sizeof(sym));
            sym.kind = hidden ? SYM_PROVIDE_HIDDEN : SYM_PROVIDE;
            advance();
            expect(ST_LPAREN, "'('");
            if (s_cur.kind == ST_IDENT || s_cur.kind == ST_DOT) {
                sym.name = dup_str(s_cur.str);
                sym.is_dot = (strcmp(s_cur.str, ".") == 0);
                advance();
            }
            expect(ST_ASSIGN, "'='");
            /* detect "PROVIDE(sym = .)" */
            if ((s_cur.kind == ST_IDENT &&
                 strcmp(s_cur.str, ".") == 0 &&
                 peek()->kind == ST_RPAREN) ||
                (s_cur.kind == ST_DOT &&
                 peek()->kind == ST_RPAREN)) {
                sym.value_is_dot = 1;
                sym.value = 0;
                advance();
            } else {
                sym.value = parse_expr();
            }
            expect(ST_RPAREN, "')'");
            add_sym(sec, &sym);
            match(ST_SEMI);
            continue;
        }

        /* symbol = expr ; */
        if (s_cur.kind == ST_IDENT && peek()->kind == ST_ASSIGN) {
            struct script_sym sym;
            memset(&sym, 0, sizeof(sym));
            sym.kind = SYM_ASSIGN;
            sym.name = dup_str(s_cur.str);
            sym.is_dot = 0;
            advance();
            advance(); /* '=' */
            /* detect "sym = ." (RHS is just location counter) */
            if ((s_cur.kind == ST_IDENT &&
                 strcmp(s_cur.str, ".") == 0 &&
                 (peek()->kind == ST_SEMI || peek()->kind == ST_RBRACE)) ||
                (s_cur.kind == ST_DOT &&
                 (peek()->kind == ST_SEMI || peek()->kind == ST_RBRACE))) {
                sym.value_is_dot = 1;
                sym.value = 0;
                advance(); /* consume '.' */
            } else {
                sym.value = parse_expr();
            }
            add_sym(sec, &sym);
            match(ST_SEMI);
            continue;
        }

        /* BYTE/SHORT/LONG/QUAD */
        if (s_cur.kind == ST_IDENT &&
            (strcmp(s_cur.str, "BYTE") == 0 ||
             strcmp(s_cur.str, "SHORT") == 0 ||
             strcmp(s_cur.str, "LONG") == 0 ||
             strcmp(s_cur.str, "QUAD") == 0)) {
            struct script_data_cmd dc;
            if (strcmp(s_cur.str, "BYTE") == 0) dc.width = 1;
            else if (strcmp(s_cur.str, "SHORT") == 0) dc.width = 2;
            else if (strcmp(s_cur.str, "LONG") == 0) dc.width = 4;
            else dc.width = 8;
            advance();
            expect(ST_LPAREN, "'('");
            dc.value = parse_expr();
            expect(ST_RPAREN, "')'");
            if (sec->ndata >= sec->data_cap) {
                sec->data_cap = sec->data_cap ? sec->data_cap * 2 : 8;
                sec->data_cmds = (struct script_data_cmd *)realloc(
                    sec->data_cmds,
                    (unsigned long)sec->data_cap *
                    sizeof(struct script_data_cmd));
            }
            sec->data_cmds[sec->ndata++] = dc;
            match(ST_SEMI);
            continue;
        }

        /* FILL(expr) - skip for now */
        if (s_cur.kind == ST_IDENT && strcmp(s_cur.str, "FILL") == 0) {
            advance();
            expect(ST_LPAREN, "'('");
            (void)parse_expr();
            expect(ST_RPAREN, "')'");
            match(ST_SEMI);
            continue;
        }

        /* KEEP(*(...)) or SORT(*)(...) or *(...) */
        {
            struct script_rule rule;
            int keep = 0;
            int sort = 0;

            memset(&rule, 0, sizeof(rule));

            if (s_cur.kind == ST_IDENT && strcmp(s_cur.str, "KEEP") == 0) {
                keep = 1;
                advance();
                expect(ST_LPAREN, "'('");
            }

            if (s_cur.kind == ST_IDENT && strcmp(s_cur.str, "SORT") == 0) {
                sort = SORT_BY_NAME;
                advance();
                expect(ST_LPAREN, "'('");
            }
            if (s_cur.kind == ST_IDENT &&
                strcmp(s_cur.str, "SORT_BY_NAME") == 0) {
                sort = SORT_BY_NAME;
                advance();
                expect(ST_LPAREN, "'('");
            }
            if (s_cur.kind == ST_IDENT &&
                strcmp(s_cur.str, "SORT_BY_ALIGNMENT") == 0) {
                sort = SORT_BY_ALIGN;
                advance();
                expect(ST_LPAREN, "'('");
            }
            if (s_cur.kind == ST_IDENT &&
                strcmp(s_cur.str, "SORT_BY_INIT_PRIORITY") == 0) {
                sort = SORT_BY_INIT_PRI;
                advance();
                expect(ST_LPAREN, "'('");
            }

            /* file pattern */
            if (s_cur.kind == ST_IDENT || s_cur.kind == ST_STAR) {
                rule.file_pattern = dup_str(
                    s_cur.kind == ST_STAR ? "*" : s_cur.str);
                advance();
            } else {
                rule.file_pattern = dup_str("*");
            }

            if (sort) {
                expect(ST_RPAREN, "')'");
            }

            rule.keep = keep;
            rule.sort = sort;

            /* ( .text .text.* .rodata ) */
            if (s_cur.kind == ST_LPAREN) {
                advance();
                while (s_cur.kind != ST_RPAREN && s_cur.kind != ST_EOF) {
                    if (s_cur.kind == ST_IDENT || s_cur.kind == ST_STAR ||
                        s_cur.kind == ST_DOT) {
                        /* handle SORT_BY_NAME(.text.*) inside pattern list */
                        if (strcmp(s_cur.str, "SORT") == 0 ||
                            strcmp(s_cur.str, "SORT_BY_NAME") == 0 ||
                            strcmp(s_cur.str, "SORT_BY_ALIGNMENT") == 0 ||
                            strcmp(s_cur.str, "SORT_BY_INIT_PRIORITY") == 0) {
                            /* propagate sort mode to rule if not yet set */
                            if (sort == SORT_NONE) {
                                if (strcmp(s_cur.str, "SORT_BY_ALIGNMENT") == 0)
                                    sort = SORT_BY_ALIGN;
                                else if (strcmp(s_cur.str,
                                         "SORT_BY_INIT_PRIORITY") == 0)
                                    sort = SORT_BY_INIT_PRI;
                                else
                                    sort = SORT_BY_NAME;
                            }
                            advance();
                            expect(ST_LPAREN, "'('");
                            while (s_cur.kind != ST_RPAREN &&
                                   s_cur.kind != ST_EOF) {
                                /* build pattern, concatenating + and * */
                                char spbuf[512];
                                int splen;
                                const char *stk;
                                stk = (s_cur.kind == ST_STAR) ? "*" :
                                      (s_cur.kind == ST_PLUS) ? "+" :
                                      s_cur.str;
                                splen = (int)strlen(stk);
                                if (splen > 511) splen = 511;
                                memcpy(spbuf, stk, (unsigned long)splen);
                                spbuf[splen] = '\0';
                                advance();
                                while (s_cur.kind != ST_RPAREN &&
                                       s_cur.kind != ST_EOF &&
                                       (s_cur.kind == ST_PLUS ||
                                        s_cur.kind == ST_STAR)) {
                                    const char *tk2;
                                    int tkl;
                                    tk2 = s_cur.kind == ST_PLUS ? "+" : "*";
                                    tkl = (int)strlen(tk2);
                                    if (splen + tkl < 511) {
                                        memcpy(spbuf + splen, tk2,
                                               (unsigned long)tkl);
                                        splen += tkl;
                                        spbuf[splen] = '\0';
                                    }
                                    advance();
                                    /* also append following ident */
                                    if (s_cur.kind == ST_IDENT) {
                                        tkl = (int)strlen(s_cur.str);
                                        if (splen + tkl < 511) {
                                            memcpy(spbuf + splen,
                                                   s_cur.str,
                                                   (unsigned long)tkl);
                                            splen += tkl;
                                            spbuf[splen] = '\0';
                                        }
                                        advance();
                                    }
                                }
                                add_pattern(&rule, spbuf);
                            }
                            expect(ST_RPAREN, "')'");
                        } else if (strcmp(s_cur.str, "EXCLUDE_FILE") == 0) {
                            /* skip EXCLUDE_FILE(...) */
                            advance();
                            expect(ST_LPAREN, "'('");
                            while (s_cur.kind != ST_RPAREN &&
                                   s_cur.kind != ST_EOF) {
                                advance();
                            }
                            expect(ST_RPAREN, "')'");
                        } else {
                            /* concatenate pattern with '+' and '*' ops
                             * e.g. ___ksymtab+* becomes one pattern */
                            char patbuf[512];
                            int plen;
                            plen = (int)strlen(s_cur.kind == ST_STAR ?
                                               "*" : s_cur.str);
                            if (plen > 511) plen = 511;
                            memcpy(patbuf,
                                   s_cur.kind == ST_STAR ? "*" : s_cur.str,
                                   (unsigned long)plen);
                            patbuf[plen] = '\0';
                            advance();
                            /* greedily append +, *, ident tokens */
                            while (s_cur.kind != ST_RPAREN &&
                                   s_cur.kind != ST_EOF &&
                                   (s_cur.kind == ST_PLUS ||
                                    s_cur.kind == ST_STAR ||
                                    (s_cur.kind == ST_IDENT &&
                                     patbuf[plen - 1] != '\0' &&
                                     (patbuf[plen - 1] == '+' ||
                                      patbuf[plen - 1] == '*')))) {
                                const char *tk;
                                int tklen;
                                if (s_cur.kind == ST_PLUS) tk = "+";
                                else if (s_cur.kind == ST_STAR) tk = "*";
                                else tk = s_cur.str;
                                tklen = (int)strlen(tk);
                                if (plen + tklen < 511) {
                                    memcpy(patbuf + plen, tk,
                                           (unsigned long)tklen);
                                    plen += tklen;
                                    patbuf[plen] = '\0';
                                }
                                advance();
                            }
                            add_pattern(&rule, patbuf);
                        }
                    } else {
                        advance();
                    }
                }
                expect(ST_RPAREN, "')'");
            }

            if (keep) {
                expect(ST_RPAREN, "')'");
            }

            /* update sort mode (may have been set by inner SORT) */
            rule.sort = sort;

            if (rule.npatterns > 0) {
                add_rule(sec, &rule);
            } else {
                /* no patterns collected, free the rule */
                free(rule.file_pattern);
                free(rule.section_patterns);
            }
        }

        match(ST_SEMI);
    }

    expect(ST_RBRACE, "'}'");
}

/* ---- top-level parser ---- */

struct ld_script *script_parse(const char *data, unsigned long size)
{
    struct ld_script *sc;

    sc = (struct ld_script *)calloc(1, sizeof(struct ld_script));
    if (!sc) { fprintf(stderr, "ld: out of memory\n"); exit(1); }

    /* save text for deferred re-evaluation */
    sc->text = (char *)malloc(size + 1);
    if (sc->text) {
        memcpy(sc->text, data, size);
        sc->text[size] = '\0';
        sc->text_len = size;
    }

    lex_init(data, size);

    while (s_cur.kind != ST_EOF) {
        /* ENTRY(sym) */
        if (s_cur.kind == ST_IDENT && strcmp(s_cur.str, "ENTRY") == 0) {
            struct script_cmd cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.kind = CMD_ENTRY;
            advance();
            expect(ST_LPAREN, "'('");
            cmd.str_arg = dup_str(s_cur.str);
            sc->entry_symbol = cmd.str_arg;
            advance();
            expect(ST_RPAREN, "')'");
            add_cmd(sc, &cmd);
            match(ST_SEMI);
            continue;
        }

        /* OUTPUT_ARCH(arch) */
        if (s_cur.kind == ST_IDENT && strcmp(s_cur.str, "OUTPUT_ARCH") == 0) {
            struct script_cmd cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.kind = CMD_OUTPUT_ARCH;
            advance();
            expect(ST_LPAREN, "'('");
            cmd.str_arg = dup_str(s_cur.str);
            advance();
            expect(ST_RPAREN, "')'");
            add_cmd(sc, &cmd);
            match(ST_SEMI);
            continue;
        }

        /* OUTPUT_FORMAT(fmt) or OUTPUT_FORMAT(fmt, big, little) */
        if (s_cur.kind == ST_IDENT &&
            strcmp(s_cur.str, "OUTPUT_FORMAT") == 0) {
            struct script_cmd cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.kind = CMD_OUTPUT_FORMAT;
            advance();
            expect(ST_LPAREN, "'('");
            cmd.str_arg = dup_str(s_cur.str);
            advance();
            /* skip optional extra args */
            while (s_cur.kind == ST_COMMA) {
                advance();
                if (s_cur.kind == ST_STR || s_cur.kind == ST_IDENT) advance();
            }
            expect(ST_RPAREN, "')'");
            add_cmd(sc, &cmd);
            match(ST_SEMI);
            continue;
        }

        /* ASSERT(expr, msg) at top level */
        if (s_cur.kind == ST_IDENT && strcmp(s_cur.str, "ASSERT") == 0) {
            struct script_cmd cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.kind = CMD_ASSERT;
            advance();
            expect(ST_LPAREN, "'('");
            /* skip the expression for now (will evaluate during execution) */
            (void)parse_expr();
            if (s_cur.kind == ST_COMMA) {
                advance();
                if (s_cur.kind == ST_STR) {
                    cmd.str_arg = dup_str(s_cur.str);
                    advance();
                } else if (s_cur.kind == ST_IDENT) {
                    cmd.str_arg = dup_str(s_cur.str);
                    advance();
                }
            }
            expect(ST_RPAREN, "')'");
            add_cmd(sc, &cmd);
            match(ST_SEMI);
            continue;
        }

        /* PROVIDE / PROVIDE_HIDDEN at top level */
        if (s_cur.kind == ST_IDENT &&
            (strcmp(s_cur.str, "PROVIDE") == 0 ||
             strcmp(s_cur.str, "PROVIDE_HIDDEN") == 0)) {
            struct script_cmd cmd;
            int hidden = (strcmp(s_cur.str, "PROVIDE_HIDDEN") == 0);
            memset(&cmd, 0, sizeof(cmd));
            cmd.kind = hidden ? CMD_PROVIDE_HIDDEN : CMD_PROVIDE;
            advance();
            expect(ST_LPAREN, "'('");
            if (s_cur.kind == ST_IDENT) {
                cmd.str_arg = dup_str(s_cur.str);
                advance();
            }
            expect(ST_ASSIGN, "'='");
            (void)parse_expr();
            expect(ST_RPAREN, "')'");
            add_cmd(sc, &cmd);
            match(ST_SEMI);
            continue;
        }

        /* top-level symbol = expr ; */
        if (s_cur.kind == ST_IDENT && peek()->kind == ST_ASSIGN) {
            struct script_cmd cmd;
            struct script_sym *sym;
            memset(&cmd, 0, sizeof(cmd));
            cmd.kind = CMD_SYMBOL;
            sym = (struct script_sym *)calloc(1, sizeof(struct script_sym));
            sym->kind = SYM_ASSIGN;
            sym->name = dup_str(s_cur.str);
            advance(); /* ident */
            advance(); /* = */
            sym->value = parse_expr();
            cmd.sym = sym;
            add_cmd(sc, &cmd);
            match(ST_SEMI);
            continue;
        }

        /* SECTIONS { ... } */
        if (s_cur.kind == ST_IDENT && strcmp(s_cur.str, "SECTIONS") == 0) {
            struct script_cmd cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.kind = CMD_SECTIONS;
            advance();
            expect(ST_LBRACE, "'{'");

            while (s_cur.kind != ST_RBRACE && s_cur.kind != ST_EOF) {
                /* location counter: . = expr ; or . += expr ; */
                if ((s_cur.kind == ST_DOT ||
                     (s_cur.kind == ST_IDENT &&
                      strcmp(s_cur.str, ".") == 0)) &&
                    (peek()->kind == ST_ASSIGN ||
                     peek()->kind == ST_PLUS_EQ ||
                     peek()->kind == ST_MINUS_EQ)) {
                    struct script_section sec;
                    struct script_sym sym;
                    int compound_op;
                    memset(&sec, 0, sizeof(sec));
                    memset(&sym, 0, sizeof(sym));
                    sec.name = dup_str(".");
                    sym.is_dot = 1;
                    sym.name = dup_str(".");
                    sym.kind = SYM_ASSIGN;
                    advance(); /* '.' */
                    compound_op = s_cur.kind;
                    advance(); /* '=' or '+=' or '-=' */
                    sym.value = parse_expr();
                    /* for += and -=, mark as addend from current dot */
                    if (compound_op == ST_PLUS_EQ) {
                        sym.value_is_dot = 1;
                        sym.addend = sym.value;
                        sym.value = 0;
                    } else if (compound_op == ST_MINUS_EQ) {
                        sym.value_is_dot = 1;
                        sym.addend = (u64)(-(i64)sym.value);
                        sym.value = 0;
                    }
                    add_sym(&sec, &sym);
                    add_section(&cmd, &sec);
                    match(ST_SEMI);
                    continue;
                }

                /* PROVIDE / PROVIDE_HIDDEN inside SECTIONS { } */
                if (s_cur.kind == ST_IDENT &&
                    (strcmp(s_cur.str, "PROVIDE") == 0 ||
                     strcmp(s_cur.str, "PROVIDE_HIDDEN") == 0)) {
                    struct script_section sec;
                    struct script_sym sym;
                    int hidden = (strcmp(s_cur.str, "PROVIDE_HIDDEN") == 0);
                    memset(&sec, 0, sizeof(sec));
                    memset(&sym, 0, sizeof(sym));
                    sec.name = dup_str(".");
                    sym.kind = hidden ? SYM_PROVIDE_HIDDEN : SYM_PROVIDE;
                    advance();
                    expect(ST_LPAREN, "'('");
                    if (s_cur.kind == ST_IDENT || s_cur.kind == ST_DOT) {
                        sym.name = dup_str(s_cur.str);
                        sym.is_dot = (strcmp(s_cur.str, ".") == 0);
                        advance();
                    }
                    expect(ST_ASSIGN, "'='");
                    sym.value = parse_expr();
                    expect(ST_RPAREN, "')'");
                    add_sym(&sec, &sym);
                    add_section(&cmd, &sec);
                    match(ST_SEMI);
                    continue;
                }

                /* symbol = expr inside SECTIONS block */
                if (s_cur.kind == ST_IDENT && peek()->kind == ST_ASSIGN) {
                    struct script_section sec;
                    struct script_sym sym;
                    memset(&sec, 0, sizeof(sec));
                    memset(&sym, 0, sizeof(sym));
                    sec.name = dup_str(".");
                    sym.kind = SYM_ASSIGN;
                    sym.name = dup_str(s_cur.str);
                    advance();
                    advance();
                    /* detect "sym = ." */
                    if ((s_cur.kind == ST_IDENT &&
                         strcmp(s_cur.str, ".") == 0 &&
                         (peek()->kind == ST_SEMI ||
                          peek()->kind == ST_RBRACE)) ||
                        (s_cur.kind == ST_DOT &&
                         (peek()->kind == ST_SEMI ||
                          peek()->kind == ST_RBRACE))) {
                        sym.value_is_dot = 1;
                        sym.value = 0;
                        advance();
                    } else {
                        sym.value = parse_expr();
                    }
                    add_sym(&sec, &sym);
                    add_section(&cmd, &sec);
                    match(ST_SEMI);
                    continue;
                }

                /* ASSERT inside SECTIONS */
                if (s_cur.kind == ST_IDENT &&
                    strcmp(s_cur.str, "ASSERT") == 0) {
                    advance();
                    expect(ST_LPAREN, "'('");
                    (void)parse_expr();
                    if (s_cur.kind == ST_COMMA) {
                        advance();
                        if (s_cur.kind == ST_STR ||
                            s_cur.kind == ST_IDENT) advance();
                    }
                    expect(ST_RPAREN, "')'");
                    match(ST_SEMI);
                    continue;
                }

                /* output section definition */
                if (s_cur.kind == ST_IDENT || s_cur.kind == ST_DOT) {
                    struct script_section sec;
                    memset(&sec, 0, sizeof(sec));
                    sec.name = dup_str(s_cur.str);

                    /* check /DISCARD/ */
                    if (strcmp(sec.name, "/DISCARD/") == 0) {
                        sec.discard = 1;
                    }

                    advance();

                    /* optional explicit address before ':' */
                    if (s_cur.kind != ST_COLON &&
                        s_cur.kind != ST_LBRACE &&
                        s_cur.kind != ST_LPAREN) {
                        sec.address = parse_expr();
                        sec.has_address = 1;
                    }

                    /* section flags: (NOLOAD), (COPY), (INFO), (OVERLAY) */
                    if (s_cur.kind == ST_LPAREN) {
                        advance();
                        if (s_cur.kind == ST_IDENT) {
                            if (strcmp(s_cur.str, "NOLOAD") == 0) {
                                sec.noload = 1;
                            }
                            /* COPY, INFO, OVERLAY: accept and skip */
                            advance();
                        }
                        expect(ST_RPAREN, "')'");
                    }

                    /* ':' separator */
                    if (s_cur.kind == ST_COLON) {
                        advance();
                    }

                    /* optional AT(addr) */
                    if (s_cur.kind == ST_IDENT &&
                        strcmp(s_cur.str, "AT") == 0) {
                        advance();
                        expect(ST_LPAREN, "'('");
                        sec.load_addr = parse_expr();
                        sec.has_load_addr = 1;
                        expect(ST_RPAREN, "')'");
                    }

                    /* optional ALIGN(n) before body */
                    if (s_cur.kind == ST_IDENT &&
                        strcmp(s_cur.str, "ALIGN") == 0 &&
                        peek()->kind == ST_LPAREN) {
                        advance();
                        expect(ST_LPAREN, "'('");
                        sec.align = parse_expr();
                        expect(ST_RPAREN, "')'");
                    }

                    /* section body { ... } */
                    if (s_cur.kind == ST_LBRACE) {
                        parse_section_body(&sec);
                    }

                    /* optional segment/phdr specs :name1 :name2 ... */
                    while (s_cur.kind == ST_COLON) {
                        advance();
                        if (s_cur.kind == ST_IDENT) {
                            if (sec.nphdr_names >= sec.phdr_name_cap) {
                                sec.phdr_name_cap = sec.phdr_name_cap ?
                                    sec.phdr_name_cap * 2 : 4;
                                sec.phdr_names = (char **)realloc(
                                    sec.phdr_names,
                                    (unsigned long)sec.phdr_name_cap *
                                    sizeof(char *));
                                if (!sec.phdr_names) {
                                    fprintf(stderr, "ld: out of memory\n");
                                    exit(1);
                                }
                            }
                            sec.phdr_names[sec.nphdr_names++] =
                                dup_str(s_cur.str);
                            advance();
                        }
                    }

                    /* optional = fill value */
                    if (s_cur.kind == ST_ASSIGN) {
                        advance();
                        (void)parse_expr();
                    }

                    add_section(&cmd, &sec);
                    continue;
                }

                /* skip unknown tokens */
                advance();
            }

            expect(ST_RBRACE, "'}'");
            add_cmd(sc, &cmd);
            continue;
        }

        /* MEMORY { ... } - skip for now */
        if (s_cur.kind == ST_IDENT && strcmp(s_cur.str, "MEMORY") == 0) {
            int depth = 0;
            advance();
            if (s_cur.kind == ST_LBRACE) {
                depth = 1;
                advance();
                while (depth > 0 && s_cur.kind != ST_EOF) {
                    if (s_cur.kind == ST_LBRACE) depth++;
                    else if (s_cur.kind == ST_RBRACE) depth--;
                    advance();
                }
            }
            continue;
        }

        /* PHDRS { name TYPE [FILEHDR] [PHDRS] [FLAGS(n)] [AT(addr)] ; ... } */
        if (s_cur.kind == ST_IDENT && strcmp(s_cur.str, "PHDRS") == 0) {
            struct script_cmd cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.kind = CMD_PHDRS;
            advance();
            expect(ST_LBRACE, "'{'");

            while (s_cur.kind != ST_RBRACE && s_cur.kind != ST_EOF) {
                struct script_phdr ph;
                memset(&ph, 0, sizeof(ph));

                /* phdr name */
                if (s_cur.kind != ST_IDENT) { advance(); continue; }
                ph.name = dup_str(s_cur.str);
                advance();

                /* segment type: PT_LOAD, PT_NOTE, PT_PHDR, etc */
                if (s_cur.kind == ST_IDENT) {
                    if (strcmp(s_cur.str, "PT_NULL") == 0) ph.type = PT_NULL;
                    else if (strcmp(s_cur.str, "PT_LOAD") == 0) ph.type = PT_LOAD;
                    else if (strcmp(s_cur.str, "PT_DYNAMIC") == 0) ph.type = PT_DYNAMIC;
                    else if (strcmp(s_cur.str, "PT_INTERP") == 0) ph.type = PT_INTERP;
                    else if (strcmp(s_cur.str, "PT_NOTE") == 0) ph.type = PT_NOTE;
                    else if (strcmp(s_cur.str, "PT_PHDR") == 0) ph.type = PT_PHDR;
                    else if (strcmp(s_cur.str, "PT_TLS") == 0) ph.type = PT_TLS;
                    else if (strcmp(s_cur.str, "PT_GNU_STACK") == 0)
                        ph.type = (u32)PT_GNU_STACK;
                    else if (strcmp(s_cur.str, "PT_GNU_RELRO") == 0)
                        ph.type = (u32)PT_GNU_RELRO;
                    else ph.type = PT_NULL;
                    advance();
                } else if (s_cur.kind == ST_NUM) {
                    ph.type = (u32)s_cur.num;
                    advance();
                }

                /* optional keywords until ';' */
                while (s_cur.kind != ST_SEMI && s_cur.kind != ST_RBRACE &&
                       s_cur.kind != ST_EOF) {
                    if (s_cur.kind == ST_IDENT &&
                        strcmp(s_cur.str, "FILEHDR") == 0) {
                        ph.filehdr = 1;
                        advance();
                    } else if (s_cur.kind == ST_IDENT &&
                               strcmp(s_cur.str, "PHDRS") == 0) {
                        ph.phdrs = 1;
                        advance();
                    } else if (s_cur.kind == ST_IDENT &&
                               strcmp(s_cur.str, "FLAGS") == 0) {
                        advance();
                        expect(ST_LPAREN, "'('");
                        ph.flags = (u32)parse_expr();
                        ph.has_flags = 1;
                        expect(ST_RPAREN, "')'");
                    } else if (s_cur.kind == ST_IDENT &&
                               strcmp(s_cur.str, "AT") == 0) {
                        advance();
                        expect(ST_LPAREN, "'('");
                        ph.at_addr = parse_expr();
                        ph.has_at = 1;
                        expect(ST_RPAREN, "')'");
                    } else {
                        advance();
                    }
                }
                match(ST_SEMI);

                /* store phdr definition */
                if (cmd.nphdrs >= cmd.phdr_cap) {
                    cmd.phdr_cap = cmd.phdr_cap ? cmd.phdr_cap * 2 : 8;
                    cmd.phdrs = (struct script_phdr *)realloc(cmd.phdrs,
                        (unsigned long)cmd.phdr_cap *
                        sizeof(struct script_phdr));
                    if (!cmd.phdrs) {
                        fprintf(stderr, "ld: out of memory\n"); exit(1);
                    }
                }
                cmd.phdrs[cmd.nphdrs++] = ph;
            }
            expect(ST_RBRACE, "'}'");

            /* cache in the script */
            sc->phdrs = cmd.phdrs;
            sc->nphdrs = cmd.nphdrs;
            add_cmd(sc, &cmd);
            continue;
        }

        /* INSERT AFTER/BEFORE section_name - accept and skip */
        if (s_cur.kind == ST_IDENT &&
            strcmp(s_cur.str, "INSERT") == 0) {
            advance();
            if (s_cur.kind == ST_IDENT &&
                (strcmp(s_cur.str, "AFTER") == 0 ||
                 strcmp(s_cur.str, "BEFORE") == 0)) {
                advance();
                /* skip section name */
                if (s_cur.kind == ST_IDENT) {
                    advance();
                }
            }
            match(ST_SEMI);
            continue;
        }

        /* skip unknown top-level tokens */
        advance();
    }

    return sc;
}

/* ---- script execution (section layout) ---- */

#define SCRIPT_BASE_ADDR  0x400000UL
#define SCRIPT_PAGE_SIZE  0x1000UL

static unsigned long align_up_s(unsigned long v, unsigned long a)
{
    if (a == 0) return v;
    return (v + a - 1) & ~(a - 1);
}

/*
 * Match an input section against the rules of an output section.
 * Returns 1 if matched (section should be placed in this output).
 */
static int section_matches(struct script_section *ss,
                           const char *filename, const char *secname)
{
    int i, j;

    for (i = 0; i < ss->nrules; i++) {
        struct script_rule *r = &ss->rules[i];

        /* check file pattern */
        if (!script_glob_match(r->file_pattern, filename)) {
            continue;
        }

        /* check section patterns */
        for (j = 0; j < r->npatterns; j++) {
            if (script_glob_match(r->section_patterns[j], secname)) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * Check if a section should be discarded.
 * Walk all /DISCARD/ output sections and test patterns.
 */
static int is_discarded(struct script_cmd *sections_cmd,
                        const char *filename, const char *secname)
{
    int i;

    for (i = 0; i < sections_cmd->nsections; i++) {
        struct script_section *ss = &sections_cmd->sections[i];
        if (ss->discard && section_matches(ss, filename, secname)) {
            return 1;
        }
    }
    return 0;
}

/*
 * Sorted section matching helpers.
 *
 * When a rule has sort != SORT_NONE, we collect all matching input
 * sections first, sort them, then add them in sorted order.
 */
struct sort_entry {
    int obj_idx;
    int sec_idx;
    const char *name;
    u64 alignment;
};

static int cmp_by_name(const void *a, const void *b)
{
    const struct sort_entry *sa = (const struct sort_entry *)a;
    const struct sort_entry *sb = (const struct sort_entry *)b;
    return strcmp(sa->name, sb->name);
}

static int cmp_by_align(const void *a, const void *b)
{
    const struct sort_entry *sa = (const struct sort_entry *)a;
    const struct sort_entry *sb = (const struct sort_entry *)b;
    /* sort descending by alignment (largest first) */
    if (sa->alignment > sb->alignment) return -1;
    if (sa->alignment < sb->alignment) return 1;
    return strcmp(sa->name, sb->name);
}

/*
 * Extract the numeric init priority from a section name like
 * ".initcall3.init" -> 3, ".initcall3s.init" -> 3.
 * Returns 999 if no digit found.
 */
static int extract_init_priority(const char *name)
{
    const char *p = name;
    int val = 0;
    int found = 0;

    /* skip to first digit */
    while (*p && (*p < '0' || *p > '9')) p++;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        found = 1;
        p++;
    }
    return found ? val : 999;
}

static int cmp_by_init_pri(const void *a, const void *b)
{
    const struct sort_entry *sa = (const struct sort_entry *)a;
    const struct sort_entry *sb = (const struct sort_entry *)b;
    int pa = extract_init_priority(sa->name);
    int pb = extract_init_priority(sb->name);
    if (pa != pb) return pa - pb;
    return strcmp(sa->name, sb->name);
}

/*
 * Helper to merge one input section into a merged output section.
 * Handles alignment, data copy, capacity growth.
 */
static void merge_input_section(struct merged_section *ms,
                                struct section *sec,
                                int obj_idx, int sec_idx)
{
    u64 sec_align, cur_size, aligned_off;

    /* propagate flags from input sections */
    if (ms->num_inputs == 0) {
        ms->shdr.sh_type = sec->shdr.sh_type;
        ms->shdr.sh_flags = sec->shdr.sh_flags;
        ms->shdr.sh_addralign = sec->shdr.sh_addralign;
    } else {
        /* merge flags: union alloc/write/exec from all inputs */
        ms->shdr.sh_flags |=
            (sec->shdr.sh_flags &
             (SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR));
    }

    sec_align = sec->shdr.sh_addralign;
    if (sec_align < 1) sec_align = 1;
    if (sec_align > ms->shdr.sh_addralign) {
        ms->shdr.sh_addralign = sec_align;
    }

    cur_size = ms->size;
    aligned_off = align_up_s((unsigned long)cur_size,
                             (unsigned long)sec_align);

    /* record input piece */
    if (ms->num_inputs >= ms->input_cap) {
        int nc = ms->input_cap == 0 ? 16 : ms->input_cap * 2;
        ms->inputs = (struct input_piece *)realloc(
            ms->inputs,
            (unsigned long)nc * sizeof(struct input_piece));
        if (!ms->inputs) {
            fprintf(stderr, "ld: out of memory\n"); exit(1);
        }
        ms->input_cap = nc;
    }
    {
        struct input_piece *ip =
            &ms->inputs[ms->num_inputs++];
        ip->obj_idx = obj_idx;
        ip->sec_idx = sec_idx;
        ip->offset_in_merged = aligned_off;
    }

    /* copy data */
    if (sec->shdr.sh_type == SHT_PROGBITS && sec->data) {
        unsigned long new_size;
        new_size = (unsigned long)(aligned_off + sec->shdr.sh_size);
        if (new_size > ms->capacity) {
            unsigned long nc;
            nc = ms->capacity == 0 ? 4096 : ms->capacity;
            while (nc < new_size) nc *= 2;
            ms->data = (u8 *)realloc(ms->data, nc);
            if (!ms->data) {
                fprintf(stderr, "ld: out of memory\n"); exit(1);
            }
            if (nc > ms->capacity) {
                memset(ms->data + ms->capacity, 0,
                       (unsigned long)(nc - ms->capacity));
            }
            ms->capacity = nc;
        }
        if (aligned_off > cur_size) {
            memset(ms->data + cur_size, 0,
                   (unsigned long)(aligned_off - cur_size));
        }
        memcpy(ms->data + aligned_off, sec->data,
               (unsigned long)sec->shdr.sh_size);
        ms->size = new_size;
    } else {
        unsigned long new_size;
        new_size = (unsigned long)(aligned_off + sec->shdr.sh_size);
        if (new_size > ms->size) {
            ms->size = new_size;
        }
    }
}

/*
 * Compute the flat index into the placed[] array for (obj_idx, sec_idx).
 */
static int placed_index(struct elf_obj *objs, int oi, int sj)
{
    int idx = 0;
    int pi;
    for (pi = 0; pi < oi; pi++) {
        idx += objs[pi].num_sections;
    }
    return idx + sj;
}

/*
 * Per-output-section phdr tracking for PHDRS execution.
 * sec_phdr_map[mi] stores which phdr indices this merged section
 * belongs to.
 */
#define MAX_PHDRS_PER_SEC 8

struct sec_phdr_info {
    int phdr_indices[MAX_PHDRS_PER_SEC];
    int nphdr;
};

/*
 * script_layout - Execute a linker script to drive section layout.
 *
 * Walks the SECTIONS command, matching input sections to output sections
 * using the script's wildcard rules. Maintains the location counter,
 * defines symbols, handles alignment, discards /DISCARD/ sections.
 *
 * When PHDRS is defined, creates program headers from the PHDRS block
 * and assigns sections to segments based on :phdr annotations.
 * When no PHDRS block, falls back to the default two-segment layout.
 *
 * Supports SORT_BY_NAME, SORT_BY_ALIGNMENT, SORT_BY_INIT_PRIORITY for
 * ordered section matching (used by kernel init call tables).
 *
 * Propagates section flags (SHF_ALLOC, SHF_WRITE, SHF_EXECINSTR) from
 * input .o files into output sections.
 */
void script_layout(struct ld_script *script,
                   struct elf_obj *objs, int num_objs,
                   struct merged_section **out_msecs, int *out_num_msecs,
                   Elf64_Phdr *out_phdrs, int *out_num_phdrs)
{
    struct merged_section *msecs;
    int nmsecs, msec_cap;
    int *placed;       /* per-object per-section: already placed? */
    int total_secs;
    u64 dot;
    u64 file_offset, hdr_size;
    int ci, si, oi, sj;
    struct exec_ctx ectx;
    int has_phdrs_block;
    struct sec_phdr_info *sec_phdr_map;
    int sec_phdr_map_cap;

    /* sort buffer for sorted matching */
    struct sort_entry *sort_buf;
    int sort_buf_cap;

    msec_cap = 32;
    msecs = (struct merged_section *)calloc((unsigned long)msec_cap,
                                            sizeof(struct merged_section));
    nmsecs = 0;

    sec_phdr_map_cap = msec_cap;
    sec_phdr_map = (struct sec_phdr_info *)calloc(
        (unsigned long)sec_phdr_map_cap, sizeof(struct sec_phdr_info));

    sort_buf_cap = 256;
    sort_buf = (struct sort_entry *)calloc(
        (unsigned long)sort_buf_cap, sizeof(struct sort_entry));

    /* track which input sections have been placed */
    total_secs = 0;
    for (oi = 0; oi < num_objs; oi++) {
        total_secs += objs[oi].num_sections;
    }
    placed = (int *)calloc((unsigned long)total_secs, sizeof(int));

    has_phdrs_block = (script->nphdrs > 0);

    hdr_size = align_up_s(
        sizeof(Elf64_Ehdr) +
        (unsigned long)(has_phdrs_block ? script->nphdrs : 4) *
        sizeof(Elf64_Phdr),
        SCRIPT_PAGE_SIZE);

    dot = SCRIPT_BASE_ADDR;

    /* set up execution context for expression evaluation */
    ectx.dot = dot;
    ectx.msecs = msecs;
    ectx.nmsecs = nmsecs;
    ectx.num_phdrs = has_phdrs_block ? script->nphdrs : 4;
    s_ectx = &ectx;

    /* walk each SECTIONS command */
    for (ci = 0; ci < script->ncmds; ci++) {
        struct script_cmd *cmd = &script->cmds[ci];
        /* handle top-level symbol and PROVIDE commands */
        if (cmd->kind == CMD_SYMBOL && cmd->sym) {
            u64 sval;
            if (cmd->sym->value_is_dot) {
                sval = dot;
            } else {
                sval = cmd->sym->value;
            }
            add_defsym(cmd->sym->name, sval);
            continue;
        }
        if (cmd->kind == CMD_PROVIDE || cmd->kind == CMD_PROVIDE_HIDDEN) {
            if (cmd->str_arg) {
                /* PROVIDE: only define if referenced but undefined */
                add_defsym(cmd->str_arg, dot);
            }
            continue;
        }
        if (cmd->kind != CMD_SECTIONS) continue;

        for (si = 0; si < cmd->nsections; si++) {
            struct script_section *ss = &cmd->sections[si];
            int mi;
            int ri;

            /* handle symbol-only pseudo sections (. = expr, sym = expr) */
            if (strcmp(ss->name, ".") == 0) {
                int k;
                for (k = 0; k < ss->nsyms; k++) {
                    struct script_sym *sym = &ss->syms[k];
                    if (sym->is_dot) {
                        if (sym->value_is_dot) {
                            /* . += N: add addend to current dot */
                            dot = dot + sym->addend;
                        } else {
                            dot = sym->value;
                        }
                        ectx.dot = dot;
                    } else if (sym->name) {
                        /* named symbol definition */
                        u64 sval;
                        if (sym->value_is_dot) {
                            sval = dot;
                        } else {
                            sval = sym->value;
                        }
                        add_defsym(sym->name, sval);
                    }
                }
                continue;
            }

            /* skip /DISCARD/ output sections */
            if (ss->discard) continue;

            /* apply explicit address */
            if (ss->has_address) {
                dot = ss->address;
                ectx.dot = dot;
            }

            /* apply alignment */
            if (ss->align > 0) {
                dot = align_up_s((unsigned long)dot,
                                 (unsigned long)ss->align);
                ectx.dot = dot;
            }

            /* handle symbols defined in section body */
            {
                int k;
                for (k = 0; k < ss->nsyms; k++) {
                    struct script_sym *sym = &ss->syms[k];
                    if (sym->is_dot) {
                        if (sym->value_is_dot) {
                            dot = dot + sym->addend;
                        } else {
                            dot = sym->value;
                        }
                        ectx.dot = dot;
                    } else if (sym->name) {
                        u64 sval;
                        if (sym->value_is_dot) {
                            sval = dot;
                        } else {
                            sval = sym->value;
                        }
                        add_defsym(sym->name, sval);
                    }
                }
            }

            /* find or create the merged output section */
            mi = -1;
            {
                int m;
                for (m = 0; m < nmsecs; m++) {
                    if (strcmp(msecs[m].name, ss->name) == 0) {
                        mi = m;
                        break;
                    }
                }
            }
            if (mi < 0) {
                if (nmsecs >= msec_cap) {
                    msec_cap *= 2;
                    msecs = (struct merged_section *)realloc(msecs,
                        (unsigned long)msec_cap *
                        sizeof(struct merged_section));
                    if (!msecs) {
                        fprintf(stderr, "ld: out of memory\n"); exit(1);
                    }
                    ectx.msecs = msecs;
                }
                if (nmsecs >= sec_phdr_map_cap) {
                    sec_phdr_map_cap *= 2;
                    sec_phdr_map = (struct sec_phdr_info *)realloc(
                        sec_phdr_map,
                        (unsigned long)sec_phdr_map_cap *
                        sizeof(struct sec_phdr_info));
                    if (!sec_phdr_map) {
                        fprintf(stderr, "ld: out of memory\n"); exit(1);
                    }
                }
                mi = nmsecs++;
                ectx.nmsecs = nmsecs;
                memset(&msecs[mi], 0, sizeof(struct merged_section));
                memset(&sec_phdr_map[mi], 0, sizeof(struct sec_phdr_info));
                msecs[mi].name = ss->name;
                if (ss->noload) {
                    msecs[mi].shdr.sh_type = SHT_NOBITS;
                } else {
                    msecs[mi].shdr.sh_type = SHT_PROGBITS;
                }
                msecs[mi].shdr.sh_flags = SHF_ALLOC;
            }

            /* record phdr assignments for this section */
            if (has_phdrs_block && ss->nphdr_names > 0) {
                int pi;
                for (pi = 0; pi < ss->nphdr_names; pi++) {
                    int phi;
                    for (phi = 0; phi < script->nphdrs; phi++) {
                        if (strcmp(script->phdrs[phi].name,
                                  ss->phdr_names[pi]) == 0) {
                            if (sec_phdr_map[mi].nphdr <
                                MAX_PHDRS_PER_SEC) {
                                sec_phdr_map[mi].phdr_indices[
                                    sec_phdr_map[mi].nphdr++] = phi;
                            }
                            break;
                        }
                    }
                }
            }

            /* match input sections - process each rule */
            for (ri = 0; ri < ss->nrules; ri++) {
                struct script_rule *rule = &ss->rules[ri];
                int sort_count = 0;

                if (rule->sort != SORT_NONE) {
                    /* collect all matching sections first, then sort */
                    sort_count = 0;

                    for (oi = 0; oi < num_objs; oi++) {
                        for (sj = 0; sj < objs[oi].num_sections; sj++) {
                            struct section *sec = &objs[oi].sections[sj];
                            int pidx;
                            int pj;
                            int matched = 0;

                            if (sec->name == NULL) continue;
                            if (sec->shdr.sh_type != SHT_PROGBITS &&
                                sec->shdr.sh_type != SHT_NOBITS &&
                                sec->shdr.sh_type != SHT_INIT_ARRAY &&
                                sec->shdr.sh_type != SHT_FINI_ARRAY)
                                continue;

                            pidx = placed_index(objs, oi, sj);
                            if (placed[pidx]) continue;

                            if (is_discarded(cmd,
                                    objs[oi].filename ?
                                    objs[oi].filename : "*",
                                    sec->name)) {
                                placed[pidx] = 1;
                                continue;
                            }

                            /* check file pattern */
                            if (!script_glob_match(rule->file_pattern,
                                    objs[oi].filename ?
                                    objs[oi].filename : "*"))
                                continue;

                            /* check section patterns */
                            for (pj = 0; pj < rule->npatterns; pj++) {
                                if (script_glob_match(
                                        rule->section_patterns[pj],
                                        sec->name)) {
                                    matched = 1;
                                    break;
                                }
                            }
                            if (!matched) continue;

                            /* add to sort buffer */
                            if (sort_count >= sort_buf_cap) {
                                sort_buf_cap *= 2;
                                sort_buf = (struct sort_entry *)realloc(
                                    sort_buf,
                                    (unsigned long)sort_buf_cap *
                                    sizeof(struct sort_entry));
                            }
                            sort_buf[sort_count].obj_idx = oi;
                            sort_buf[sort_count].sec_idx = sj;
                            sort_buf[sort_count].name = sec->name;
                            sort_buf[sort_count].alignment =
                                sec->shdr.sh_addralign;
                            sort_count++;
                        }
                    }

                    /* sort the collected sections */
                    if (sort_count > 1) {
                        if (rule->sort == SORT_BY_ALIGN) {
                            qsort(sort_buf, (unsigned long)sort_count,
                                  sizeof(struct sort_entry), cmp_by_align);
                        } else if (rule->sort == SORT_BY_INIT_PRI) {
                            qsort(sort_buf, (unsigned long)sort_count,
                                  sizeof(struct sort_entry),
                                  cmp_by_init_pri);
                        } else {
                            qsort(sort_buf, (unsigned long)sort_count,
                                  sizeof(struct sort_entry), cmp_by_name);
                        }
                    }

                    /* add sorted sections to merged output */
                    {
                        int si2;
                        for (si2 = 0; si2 < sort_count; si2++) {
                            int o2 = sort_buf[si2].obj_idx;
                            int s2 = sort_buf[si2].sec_idx;
                            struct section *sec = &objs[o2].sections[s2];
                            int pidx = placed_index(objs, o2, s2);
                            placed[pidx] = 1;
                            merge_input_section(&msecs[mi], sec, o2, s2);
                        }
                    }
                } else {
                    /* unsorted: match and add in input order */
                    for (oi = 0; oi < num_objs; oi++) {
                        for (sj = 0; sj < objs[oi].num_sections; sj++) {
                            struct section *sec = &objs[oi].sections[sj];
                            int pidx;
                            int pj;
                            int matched = 0;

                            if (sec->name == NULL) continue;
                            if (sec->shdr.sh_type != SHT_PROGBITS &&
                                sec->shdr.sh_type != SHT_NOBITS &&
                                sec->shdr.sh_type != SHT_INIT_ARRAY &&
                                sec->shdr.sh_type != SHT_FINI_ARRAY)
                                continue;

                            pidx = placed_index(objs, oi, sj);
                            if (placed[pidx]) continue;

                            if (is_discarded(cmd,
                                    objs[oi].filename ?
                                    objs[oi].filename : "*",
                                    sec->name)) {
                                placed[pidx] = 1;
                                continue;
                            }

                            if (!script_glob_match(rule->file_pattern,
                                    objs[oi].filename ?
                                    objs[oi].filename : "*"))
                                continue;

                            for (pj = 0; pj < rule->npatterns; pj++) {
                                if (script_glob_match(
                                        rule->section_patterns[pj],
                                        sec->name)) {
                                    matched = 1;
                                    break;
                                }
                            }
                            if (!matched) continue;

                            placed[pidx] = 1;
                            merge_input_section(&msecs[mi], sec, oi, sj);
                        }
                    }
                }
            }

            /* also try matching sections not covered by explicit rules
             * (for output sections with no rules, e.g. just wildcards) */
            if (ss->nrules == 0) {
                /* no rules: just a placeholder section */
            }

            /* advance dot past this section */
            dot += msecs[mi].size;
            ectx.dot = dot;
        }
    }

    free(placed);
    free(sort_buf);

    /*
     * Phase 2: assign file offsets and virtual addresses.
     * Same two-segment layout as layout.c (RX text, RW data).
     */
    file_offset = hdr_size;
    dot = SCRIPT_BASE_ADDR + file_offset;

    /* text/readonly sections */
    for (si = 0; si < nmsecs; si++) {
        u64 a;
        int j;

        if (msecs[si].shdr.sh_flags & SHF_WRITE) continue;

        a = msecs[si].shdr.sh_addralign;
        if (a < 1) a = 1;

        file_offset = align_up_s((unsigned long)file_offset, (unsigned long)a);
        dot = align_up_s((unsigned long)dot, (unsigned long)a);

        msecs[si].shdr.sh_offset = file_offset;
        msecs[si].shdr.sh_addr = dot;
        msecs[si].shdr.sh_size = msecs[si].size;

        for (j = 0; j < msecs[si].num_inputs; j++) {
            struct input_piece *ip = &msecs[si].inputs[j];
            objs[ip->obj_idx].sections[ip->sec_idx].out_addr =
                dot + ip->offset_in_merged;
        }

        if (msecs[si].shdr.sh_type != SHT_NOBITS) {
            file_offset += msecs[si].size;
        }
        dot += msecs[si].size;
    }

    /* new page for data */
    file_offset = align_up_s((unsigned long)file_offset, SCRIPT_PAGE_SIZE);
    dot = align_up_s((unsigned long)dot, SCRIPT_PAGE_SIZE);

    for (si = 0; si < nmsecs; si++) {
        u64 a;
        int j;

        if (!(msecs[si].shdr.sh_flags & SHF_WRITE)) continue;

        a = msecs[si].shdr.sh_addralign;
        if (a < 1) a = 1;

        file_offset = align_up_s((unsigned long)file_offset, (unsigned long)a);
        dot = align_up_s((unsigned long)dot, (unsigned long)a);

        msecs[si].shdr.sh_offset = file_offset;
        msecs[si].shdr.sh_addr = dot;
        msecs[si].shdr.sh_size = msecs[si].size;

        for (j = 0; j < msecs[si].num_inputs; j++) {
            struct input_piece *ip = &msecs[si].inputs[j];
            objs[ip->obj_idx].sections[ip->sec_idx].out_addr =
                dot + ip->offset_in_merged;
        }

        if (msecs[si].shdr.sh_type != SHT_NOBITS) {
            file_offset += msecs[si].size;
        }
        dot += msecs[si].size;
    }

    /*
     * Phase 3: Build program headers.
     *
     * If the script defined a PHDRS block, create segments from those
     * definitions and assign sections to them based on the :phdr
     * annotations. Otherwise, fall back to the default two-segment
     * layout (RX text + RW data).
     */
    if (has_phdrs_block) {
        int phi;
        *out_num_phdrs = script->nphdrs;
        memset(out_phdrs, 0,
               (unsigned long)script->nphdrs * sizeof(Elf64_Phdr));

        for (phi = 0; phi < script->nphdrs; phi++) {
            struct script_phdr *ph = &script->phdrs[phi];
            Elf64_Phdr *op = &out_phdrs[phi];
            u64 seg_lo_off = (u64)-1;
            u64 seg_hi_off = 0;
            u64 seg_lo_va = (u64)-1;
            u64 seg_hi_va = 0;
            u64 seg_memsz_end = 0;
            u32 derived_flags = 0;
            int has_sections = 0;

            op->p_type = ph->type;

            /* scan all merged sections for those assigned to this phdr */
            for (si = 0; si < nmsecs; si++) {
                int k;
                int found = 0;

                for (k = 0; k < sec_phdr_map[si].nphdr; k++) {
                    if (sec_phdr_map[si].phdr_indices[k] == phi) {
                        found = 1;
                        break;
                    }
                }
                if (!found) continue;

                has_sections = 1;

                /* derive flags from section permissions */
                if (msecs[si].shdr.sh_flags & SHF_EXECINSTR)
                    derived_flags |= PF_X;
                if (msecs[si].shdr.sh_flags & SHF_WRITE)
                    derived_flags |= PF_W;
                if (msecs[si].shdr.sh_flags & SHF_ALLOC)
                    derived_flags |= PF_R;

                /* track file offset range */
                if (msecs[si].shdr.sh_offset < seg_lo_off) {
                    seg_lo_off = msecs[si].shdr.sh_offset;
                }
                if (msecs[si].shdr.sh_type != SHT_NOBITS) {
                    u64 end = msecs[si].shdr.sh_offset + msecs[si].size;
                    if (end > seg_hi_off) seg_hi_off = end;
                }

                /* track vaddr range */
                if (msecs[si].shdr.sh_addr < seg_lo_va) {
                    seg_lo_va = msecs[si].shdr.sh_addr;
                }
                {
                    u64 va_end = msecs[si].shdr.sh_addr + msecs[si].size;
                    if (va_end > seg_memsz_end) seg_memsz_end = va_end;
                }
                {
                    u64 va_end2 = msecs[si].shdr.sh_addr + msecs[si].size;
                    if (va_end2 > seg_hi_va) seg_hi_va = va_end2;
                }
            }

            /* set flags: explicit FLAGS() overrides derived */
            if (ph->has_flags) {
                op->p_flags = ph->flags;
            } else if (has_sections) {
                op->p_flags = derived_flags;
            } else {
                op->p_flags = PF_R;
            }

            if (ph->type == PT_PHDR) {
                /* PT_PHDR covers the program header table itself */
                op->p_offset = sizeof(Elf64_Ehdr);
                op->p_vaddr = SCRIPT_BASE_ADDR + sizeof(Elf64_Ehdr);
                op->p_paddr = op->p_vaddr;
                op->p_filesz = (u64)script->nphdrs * sizeof(Elf64_Phdr);
                op->p_memsz = op->p_filesz;
                op->p_align = 8;
                if (!ph->has_flags)
                    op->p_flags = PF_R;
            } else if (has_sections) {
                op->p_offset = seg_lo_off;
                op->p_vaddr = seg_lo_va;
                op->p_paddr = ph->has_at ? ph->at_addr : seg_lo_va;
                op->p_filesz = seg_hi_off > seg_lo_off ?
                    seg_hi_off - seg_lo_off : 0;
                op->p_memsz = seg_memsz_end > seg_lo_va ?
                    seg_memsz_end - seg_lo_va : 0;
                op->p_align = SCRIPT_PAGE_SIZE;

                /* if FILEHDR, extend backward to include ELF header */
                if (ph->filehdr) {
                    u64 hdr_end = sizeof(Elf64_Ehdr);
                    if (ph->phdrs) {
                        hdr_end += (u64)script->nphdrs *
                                   sizeof(Elf64_Phdr);
                    }
                    if (op->p_offset > 0) {
                        u64 extend = op->p_offset;
                        op->p_offset = 0;
                        op->p_vaddr -= extend;
                        op->p_paddr -= extend;
                        op->p_filesz += extend;
                        op->p_memsz += extend;
                    }
                }
            } else {
                /* empty segment (e.g. PT_GNU_STACK) */
                op->p_offset = 0;
                op->p_vaddr = 0;
                op->p_paddr = 0;
                op->p_filesz = 0;
                op->p_memsz = 0;
                op->p_align = ph->type == PT_GNU_STACK ? 16 :
                              SCRIPT_PAGE_SIZE;
            }
        }
    } else {
        /* default two-segment layout */
        u64 text_seg_end = hdr_size;
        u64 data_seg_start = 0;
        u64 data_seg_file_start = 0;
        u64 data_seg_filesz = 0;
        u64 data_seg_memsz = 0;

        for (si = 0; si < nmsecs; si++) {
            if (msecs[si].shdr.sh_flags & SHF_WRITE) continue;
            if (msecs[si].shdr.sh_type != SHT_NOBITS) {
                u64 end = msecs[si].shdr.sh_offset + msecs[si].size;
                if (end > text_seg_end) text_seg_end = end;
            }
        }

        data_seg_file_start = align_up_s((unsigned long)text_seg_end,
                                         SCRIPT_PAGE_SIZE);
        for (si = 0; si < nmsecs; si++) {
            if (!(msecs[si].shdr.sh_flags & SHF_WRITE)) continue;
            if (data_seg_start == 0)
                data_seg_start = msecs[si].shdr.sh_addr;
            if (msecs[si].shdr.sh_type != SHT_NOBITS) {
                u64 end = msecs[si].shdr.sh_offset + msecs[si].size;
                data_seg_filesz = end - data_seg_file_start;
            }
            {
                u64 va_end = msecs[si].shdr.sh_addr + msecs[si].size;
                data_seg_memsz = va_end - data_seg_start;
            }
        }

        memset(out_phdrs, 0, 4 * sizeof(Elf64_Phdr));

        out_phdrs[0].p_type = PT_LOAD;
        out_phdrs[0].p_flags = PF_R | PF_X;
        out_phdrs[0].p_offset = 0;
        out_phdrs[0].p_vaddr = SCRIPT_BASE_ADDR;
        out_phdrs[0].p_paddr = SCRIPT_BASE_ADDR;
        out_phdrs[0].p_filesz = text_seg_end;
        out_phdrs[0].p_memsz = out_phdrs[0].p_filesz;
        out_phdrs[0].p_align = SCRIPT_PAGE_SIZE;
        *out_num_phdrs = 1;

        if (data_seg_memsz > 0) {
            out_phdrs[1].p_type = PT_LOAD;
            out_phdrs[1].p_flags = PF_R | PF_W;
            out_phdrs[1].p_offset = data_seg_file_start;
            out_phdrs[1].p_vaddr = data_seg_start;
            out_phdrs[1].p_paddr = data_seg_start;
            out_phdrs[1].p_filesz = data_seg_filesz;
            out_phdrs[1].p_memsz = data_seg_memsz;
            out_phdrs[1].p_align = SCRIPT_PAGE_SIZE;
            *out_num_phdrs = 2;
        }
    }

    free(sec_phdr_map);

    /*
     * Phase 4: Re-evaluate deferred top-level symbol assignments.
     * These reference symbols defined during layout (e.g. _end, _text).
     * Re-parse the entire script with the fully-populated symbol table
     * and only process top-level symbol and PROVIDE commands.
     */
    if (script->text && script->text_len > 0) {
        lex_init(script->text, script->text_len);
        while (s_cur.kind != ST_EOF) {
            /* top-level symbol = expr ; */
            if (s_cur.kind == ST_IDENT && peek()->kind == ST_ASSIGN) {
                char sym_name[256];
                u64 sval;
                int slen = (int)strlen(s_cur.str);
                if (slen > 255) slen = 255;
                memcpy(sym_name, s_cur.str, (unsigned long)slen);
                sym_name[slen] = '\0';
                advance(); /* ident */
                advance(); /* = */
                sval = parse_expr();
                add_defsym(sym_name, sval);
                match(ST_SEMI);
                continue;
            }
            /* PROVIDE(sym = expr) / PROVIDE_HIDDEN(sym = expr) */
            if (s_cur.kind == ST_IDENT &&
                (strcmp(s_cur.str, "PROVIDE") == 0 ||
                 strcmp(s_cur.str, "PROVIDE_HIDDEN") == 0)) {
                char pname[256];
                u64 pval;
                advance();
                expect(ST_LPAREN, "'('");
                if (s_cur.kind == ST_IDENT) {
                    int pnlen = (int)strlen(s_cur.str);
                    if (pnlen > 255) pnlen = 255;
                    memcpy(pname, s_cur.str, (unsigned long)pnlen);
                    pname[pnlen] = '\0';
                    advance();
                } else {
                    pname[0] = '\0';
                }
                expect(ST_ASSIGN, "'='");
                pval = parse_expr();
                expect(ST_RPAREN, "')'");
                if (pname[0]) {
                    add_defsym(pname, pval);
                }
                match(ST_SEMI);
                continue;
            }
            /* ASSERT at top level - re-evaluate */
            if (s_cur.kind == ST_IDENT &&
                strcmp(s_cur.str, "ASSERT") == 0) {
                advance();
                expect(ST_LPAREN, "'('");
                (void)parse_expr();
                if (s_cur.kind == ST_COMMA) {
                    advance();
                    if (s_cur.kind == ST_STR ||
                        s_cur.kind == ST_IDENT) advance();
                }
                expect(ST_RPAREN, "')'");
                match(ST_SEMI);
                continue;
            }
            /* skip braced blocks */
            if (s_cur.kind == ST_LBRACE) {
                int depth = 1;
                advance();
                while (depth > 0 && s_cur.kind != ST_EOF) {
                    if (s_cur.kind == ST_LBRACE) depth++;
                    else if (s_cur.kind == ST_RBRACE) depth--;
                    advance();
                }
                continue;
            }
            advance();
        }
    }

    s_ectx = NULL;

    *out_msecs = msecs;
    *out_num_msecs = nmsecs;
}

/* ---- script file I/O ---- */

struct ld_script *script_read(const char *path)
{
    FILE *f;
    char *buf;
    long len;
    struct ld_script *sc;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ld: cannot open script '%s'\n", path);
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = (char *)malloc((unsigned long)len + 1);
    if (!buf) {
        fprintf(stderr, "ld: out of memory\n");
        fclose(f);
        exit(1);
    }

    if (fread(buf, 1, (unsigned long)len, f) != (unsigned long)len) {
        fprintf(stderr, "ld: read error on '%s'\n", path);
        fclose(f);
        free(buf);
        exit(1);
    }
    buf[len] = '\0';
    fclose(f);

    sc = script_parse(buf, (unsigned long)len);
    free(buf);
    return sc;
}

/* ---- cleanup ---- */

void script_free(struct ld_script *sc)
{
    int i, j, k;

    if (!sc) return;

    for (i = 0; i < sc->ncmds; i++) {
        struct script_cmd *cmd = &sc->cmds[i];
        free(cmd->str_arg);

        for (j = 0; j < cmd->nsections; j++) {
            struct script_section *sec = &cmd->sections[j];

            for (k = 0; k < sec->nrules; k++) {
                int p;
                free(sec->rules[k].file_pattern);
                for (p = 0; p < sec->rules[k].npatterns; p++) {
                    free(sec->rules[k].section_patterns[p]);
                }
                free(sec->rules[k].section_patterns);
            }
            free(sec->rules);

            for (k = 0; k < sec->nsyms; k++) {
                free(sec->syms[k].name);
            }
            free(sec->syms);
            free(sec->data_cmds);

            /* free phdr name annotations */
            for (k = 0; k < sec->nphdr_names; k++) {
                free(sec->phdr_names[k]);
            }
            free(sec->phdr_names);

            free(sec->name);
        }
        free(cmd->sections);

        /* free PHDRS definitions */
        for (j = 0; j < cmd->nphdrs; j++) {
            free(cmd->phdrs[j].name);
        }
        free(cmd->phdrs);

        free(cmd->sym);
    }
    free(sc->cmds);
    free(sc);
}

const char *script_entry(struct ld_script *sc)
{
    return sc ? sc->entry_symbol : NULL;
}
