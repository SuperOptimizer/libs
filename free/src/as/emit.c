/*
 * emit.c - ELF object file emitter for the free aarch64 assembler
 * Parses assembly tokens, encodes instructions, and writes ELF .o files.
 * Pure C89. No external dependencies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "free.h"
#include "elf.h"
#include "aarch64.h"

/* ---- Assembly token types (must match lex.c) ---- */
enum as_tok_kind {
    ASTOK_EOF = 0,
    ASTOK_NEWLINE,
    ASTOK_IDENT,
    ASTOK_LABEL,
    ASTOK_DIRECTIVE,
    ASTOK_REG,
    ASTOK_FPREG,
    ASTOK_IMM,
    ASTOK_NUM,
    ASTOK_STRING,
    ASTOK_COMMA,
    ASTOK_LBRACKET,
    ASTOK_RBRACKET,
    ASTOK_EXCL,
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
    long val;
    int is_wreg;
    int is_sreg;
    int line;
};

struct as_lexer {
    const char *src;
    const char *pos;
    int line;
};

/* from lex.c */
void as_lex_init(struct as_lexer *lex, const char *src);
void as_next_token(struct as_lexer *lex, struct as_token *tok);

/* ---- Constants ---- */
#define MAX_SYMS     8192
#define MAX_RELOCS   16384
#define MAX_STRTAB   (128 * 1024)
#define MAX_SHSTRTAB 4096

/* ---- Dynamic section table ---- */
#define MAX_SECTIONS      128
#define SEC_INIT_CAP      4096   /* initial buffer capacity */

struct asm_section {
    char name[128];
    u32 sh_type;       /* SHT_PROGBITS, SHT_NOBITS, SHT_NOTE, etc. */
    u64 sh_flags;      /* SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR ... */
    u8 *data;          /* content buffer (NULL for SHT_NOBITS) */
    u32 size;          /* bytes emitted */
    u32 capacity;      /* allocated size of data buffer */
    u32 alignment;     /* section alignment (max seen from .align) */
    u32 entsize;       /* entry size for SHF_MERGE sections */
};

/* Well-known section indices (pre-created) */
#define SEC_NULL    0
#define SEC_TEXT    1
#define SEC_DATA    2
#define SEC_RODATA  3
#define SEC_BSS     4
#define NUM_PREDEF  5  /* number of pre-created content sections */

/* ---- Symbol ---- */
struct asm_sym {
    char name[128];
    int section;       /* SEC_TEXT, SEC_DATA, etc */
    u64 value;         /* offset within section */
    int is_global;
    int is_defined;
    int sym_type;      /* STT_NOTYPE, STT_FUNC, etc */
    u64 sym_size;      /* .size value */
    int index;         /* index in final symbol table */
    int binding;       /* STB_LOCAL, STB_GLOBAL, STB_WEAK */
    int visibility;    /* STV_DEFAULT, STV_HIDDEN */
};

/* ---- Macro definition ---- */
#define MAX_MACROS       256
#define MAX_MACRO_PARAMS 16
#define MAX_MACRO_BODY   8192

struct asm_macro {
    char name[64];
    char params[MAX_MACRO_PARAMS][64];
    int num_params;
    char body[MAX_MACRO_BODY];
    int body_len;
};

/* ---- Register aliases (.req / .unreq) ---- */
#define MAX_REG_ALIASES 64
struct reg_alias {
    char name[64];
    int reg;           /* register number (0-30, 31=sp/xzr) */
    int is_wreg;       /* 1 if w-form */
};

/* ---- Local numeric label tracking ---- */
#define MAX_LOCAL_LABELS 1024
struct local_label {
    int digit;         /* numeric label value (0-9, or multi-digit like 661) */
    int section;
    u64 value;
    int seq;           /* sequence number for ordering */
};

/* ---- Section stack ---- */
#define MAX_SEC_STACK 16

/* ---- Conditional assembly ---- */
#define MAX_COND_DEPTH 16

/* ---- Repeat block ---- */
#define MAX_REPT_BODY  4096

/* ---- IRP/IRPC block ---- */
#define MAX_IRP_BODY   4096
#define MAX_IRP_VALUES 64

/* ---- Relocation ---- */
struct asm_reloc {
    u64 offset;        /* offset within section where reloc applies */
    int sym_index;     /* index into our sym array */
    int rtype;         /* R_AARCH64_* */
    i64 addend;
    int section;       /* section index this reloc belongs to */
};

/* ---- Assembler state ---- */
struct assembler {
    /* dynamic section table */
    struct asm_section sections[MAX_SECTIONS];
    int num_sections;

    /* current section index */
    int cur_section;

    /* symbols */
    struct asm_sym syms[MAX_SYMS];
    int num_syms;

    /* relocations */
    struct asm_reloc relocs[MAX_RELOCS];
    int num_relocs;

    /* string tables */
    char strtab[MAX_STRTAB];
    u32 strtab_size;
    char shstrtab[MAX_SHSTRTAB];
    u32 shstrtab_size;

    /* lexer */
    struct as_lexer lex;
    struct as_token tok;

    /* pass number (1 = collect labels, 2 = encode) */
    int pass;

    /* macros */
    struct asm_macro macros[MAX_MACROS];
    int num_macros;
    int in_macro_def;    /* 1 if currently recording macro body */
    int cur_macro;       /* index of macro being defined */

    /* conditional assembly */
    int cond_stack[MAX_COND_DEPTH];  /* 1 = active, 0 = skipping */
    int cond_depth;
    int cond_met[MAX_COND_DEPTH];    /* condition was true at some level */

    /* section stack */
    int sec_stack[MAX_SEC_STACK];
    int sec_stack_top;
    int prev_section;    /* for .previous */

    /* local numeric labels */
    struct local_label local_labels[MAX_LOCAL_LABELS];
    int num_local_labels;
    int local_label_seq;

    /* repeat block */
    int in_rept;
    int rept_count;
    char rept_body[MAX_REPT_BODY];
    int rept_body_len;

    /* register aliases */
    struct reg_alias reg_aliases[MAX_REG_ALIASES];
    int num_reg_aliases;

    /* IRP/IRPC block */
    int in_irp;            /* 1 = .irp, 2 = .irpc */
    char irp_var[64];      /* iteration variable name */
    char irp_values[MAX_IRP_VALUES][128]; /* values for .irp */
    int irp_num_values;
    char irp_chars[256];   /* characters for .irpc */
    int irp_num_chars;
    char irp_body[MAX_IRP_BODY];
    int irp_body_len;
};

/* ---- Forward declarations ---- */
static int tok_eq(const struct as_token *t, const char *s);
static void emit_byte(struct assembler *as, u8 b);
static void emit_u16(struct assembler *as, u16 v);
static void emit_u32(struct assembler *as, u32 v);
static void emit_u64(struct assembler *as, u64 v);
static void emit_insn(struct assembler *as, u32 insn);
static int find_sym(struct assembler *as, const char *name, int len);
static int add_sym(struct assembler *as, const char *name, int len);
static void add_reloc(struct assembler *as, int sym, int rtype, i64 addend);
static u32 add_strtab(struct assembler *as, const char *name, int len);
static u32 add_shstrtab(struct assembler *as, const char *name);
static u32 cur_offset(struct assembler *as);
static void advance(struct assembler *as);
static void expect(struct assembler *as, enum as_tok_kind kind);
static void skip_newlines(struct assembler *as);
static void parse_directive(struct assembler *as);
static void parse_instruction(struct assembler *as);
static int parse_cond(const char *name, int len);
static int lookup_mnemonic(const char *name, int len);
static void align_section(struct assembler *as, int alignment);
static int unescape_string(const char *src, int srclen, u8 *dst, int dstmax);
/* forward declarations for assembler macro/conditional support (wiring in progress) */
static int is_cond_active(struct assembler *as) __attribute__((unused));
static int find_macro(struct assembler *as, const char *name, int len) __attribute__((unused));
static void expand_macro(struct assembler *as, int macro_idx) __attribute__((unused));
static int find_reg_alias(struct assembler *as, const char *name, int len);
static void skip_rest_of_line(struct assembler *as);
static void add_local_label(struct assembler *as, int digit) __attribute__((unused));
static int resolve_local_label(struct assembler *as, int digit, int forward);
static i64 parse_expr_value(struct assembler *as);
static void assemble_pass(struct assembler *as, const char *src);
static int find_or_create_section(struct assembler *as, const char *name,
                                  int namelen, u32 type, u64 flags);
static void init_sections(struct assembler *as);

/* ---- Token comparison ---- */

static int tok_eq(const struct as_token *t, const char *s)
{
    int i;
    int slen = 0;

    while (s[slen]) slen++;
    if (t->len != slen) return 0;
    for (i = 0; i < slen; i++) {
        if (t->start[i] != s[i]) return 0;
    }
    return 1;
}

/* ---- Dynamic section management ---- */

static void section_ensure(struct asm_section *sec, u32 needed)
{
    u32 new_cap;
    u8 *new_buf;

    if (needed <= sec->capacity) return;
    new_cap = sec->capacity == 0 ? SEC_INIT_CAP : sec->capacity;
    while (new_cap < needed) new_cap *= 2;
    new_buf = (u8 *)realloc(sec->data, new_cap);
    if (!new_buf) {
        fprintf(stderr, "as: out of memory for section '%s'\n", sec->name);
        exit(1);
    }
    sec->data = new_buf;
    sec->capacity = new_cap;
}

static int find_section(struct assembler *as, const char *name, int namelen)
{
    int i;
    int nlen;

    for (i = 0; i < as->num_sections; i++) {
        nlen = (int)strlen(as->sections[i].name);
        if (nlen == namelen &&
            strncmp(as->sections[i].name, name, (size_t)namelen) == 0)
            return i;
    }
    return -1;
}

static int find_or_create_section(struct assembler *as, const char *name,
                                  int namelen, u32 type, u64 flags)
{
    int idx;
    struct asm_section *sec;

    idx = find_section(as, name, namelen);
    if (idx >= 0) return idx;

    if (as->num_sections >= MAX_SECTIONS) {
        fprintf(stderr, "as: too many sections\n");
        exit(1);
    }

    idx = as->num_sections++;
    sec = &as->sections[idx];
    memset(sec, 0, sizeof(*sec));
    if (namelen > 127) namelen = 127;
    memcpy(sec->name, name, (size_t)namelen);
    sec->name[namelen] = '\0';
    sec->sh_type = type;
    sec->sh_flags = flags;
    sec->data = NULL;
    sec->size = 0;
    sec->capacity = 0;
    sec->alignment = 1;
    sec->entsize = 0;
    return idx;
}

static void init_sections(struct assembler *as)
{
    /* Section 0: null */
    as->num_sections = 0;
    find_or_create_section(as, "", 0, SHT_NULL, 0);

    /* Pre-create standard sections */
    find_or_create_section(as, ".text", 5,
                           SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    as->sections[SEC_TEXT].alignment = 4;

    find_or_create_section(as, ".data", 5,
                           SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    as->sections[SEC_DATA].alignment = 8;

    find_or_create_section(as, ".rodata", 7,
                           SHT_PROGBITS, SHF_ALLOC);
    as->sections[SEC_RODATA].alignment = 8;

    find_or_create_section(as, ".bss", 4,
                           SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
    as->sections[SEC_BSS].alignment = 8;
}

/* ---- Section data emission ---- */

static u32 cur_offset(struct assembler *as)
{
    return as->sections[as->cur_section].size;
}

static void emit_byte(struct assembler *as, u8 b)
{
    struct asm_section *sec;

    if (as->pass != 2) return;
    sec = &as->sections[as->cur_section];

    if (sec->sh_type == SHT_NOBITS) {
        /* BSS-like: just track size, no data buffer */
        sec->size++;
        return;
    }

    section_ensure(sec, sec->size + 1);
    sec->data[sec->size] = b;
    sec->size++;
}

static void emit_u16(struct assembler *as, u16 v)
{
    emit_byte(as, (u8)(v & 0xFF));
    emit_byte(as, (u8)((v >> 8) & 0xFF));
}

static void emit_u32(struct assembler *as, u32 v)
{
    emit_byte(as, (u8)(v & 0xFF));
    emit_byte(as, (u8)((v >> 8) & 0xFF));
    emit_byte(as, (u8)((v >> 16) & 0xFF));
    emit_byte(as, (u8)((v >> 24) & 0xFF));
}

static void emit_u64(struct assembler *as, u64 v)
{
    emit_u32(as, (u32)(v & 0xFFFFFFFF));
    emit_u32(as, (u32)((v >> 32) & 0xFFFFFFFF));
}

static void emit_insn(struct assembler *as, u32 insn)
{
    emit_u32(as, insn);
}

/* ---- Symbol management ---- */

static int find_sym(struct assembler *as, const char *name, int len)
{
    int i;
    int nlen;

    for (i = 0; i < as->num_syms; i++) {
        nlen = (int)strlen(as->syms[i].name);
        if (nlen == len && strncmp(as->syms[i].name, name, (size_t)len) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_sym(struct assembler *as, const char *name, int len)
{
    int idx;
    struct asm_sym *s;

    idx = find_sym(as, name, len);
    if (idx >= 0) return idx;

    if (as->num_syms >= MAX_SYMS) {
        fprintf(stderr, "as: too many symbols\n");
        exit(1);
    }

    s = &as->syms[as->num_syms];
    if (len > 127) len = 127;
    memcpy(s->name, name, (size_t)len);
    s->name[len] = '\0';
    s->section = SEC_NULL;
    s->value = 0;
    s->is_global = 0;
    s->is_defined = 0;
    s->sym_type = STT_NOTYPE;
    s->sym_size = 0;
    s->index = 0;
    s->binding = STB_LOCAL;
    s->visibility = STV_DEFAULT;

    return as->num_syms++;
}

/* ---- Relocation management ---- */

static void add_reloc(struct assembler *as, int sym, int rtype, i64 addend)
{
    struct asm_reloc *r;

    if (as->pass != 2) return;
    if (as->num_relocs >= MAX_RELOCS) {
        fprintf(stderr, "as: too many relocations\n");
        exit(1);
    }

    r = &as->relocs[as->num_relocs++];
    r->offset = cur_offset(as);
    r->sym_index = sym;
    r->rtype = rtype;
    r->addend = addend;
    r->section = as->cur_section;
}

/* ---- String table management ---- */

static u32 add_strtab(struct assembler *as, const char *name, int len)
{
    u32 pos = as->strtab_size;

    if (as->strtab_size + (u32)len + 1 > MAX_STRTAB) {
        fprintf(stderr, "as: strtab overflow\n");
        exit(1);
    }
    memcpy(as->strtab + as->strtab_size, name, (size_t)len);
    as->strtab_size += (u32)len;
    as->strtab[as->strtab_size++] = '\0';
    return pos;
}

static u32 add_shstrtab(struct assembler *as, const char *name)
{
    u32 pos = as->shstrtab_size;
    int len = (int)strlen(name);

    if (as->shstrtab_size + (u32)len + 1 > MAX_SHSTRTAB) {
        fprintf(stderr, "as: shstrtab overflow\n");
        exit(1);
    }
    memcpy(as->shstrtab + as->shstrtab_size, name, (size_t)len + 1);
    as->shstrtab_size += (u32)len + 1;
    return pos;
}

/* ---- Lexer helpers ---- */

static void advance(struct assembler *as)
{
    as_next_token(&as->lex, &as->tok);
    /* resolve register aliases: if token is an IDENT that matches
     * a register alias, convert it to ASTOK_REG */
    if (as->tok.kind == ASTOK_IDENT && as->num_reg_aliases > 0) {
        int ai = find_reg_alias(as, as->tok.start, as->tok.len);
        if (ai >= 0) {
            as->tok.kind = ASTOK_REG;
            as->tok.val = as->reg_aliases[ai].reg;
            as->tok.is_wreg = as->reg_aliases[ai].is_wreg;
        }
    }
}

static void expect(struct assembler *as, enum as_tok_kind kind)
{
    if (as->tok.kind != kind) {
        fprintf(stderr, "as: line %d: unexpected token (kind=%d, expected=%d)\n",
                as->tok.line, as->tok.kind, kind);
        exit(1);
    }
    advance(as);
}

static void skip_newlines(struct assembler *as)
{
    while (as->tok.kind == ASTOK_NEWLINE) {
        advance(as);
    }
}

/* ---- String unescape ---- */

static int unescape_string(const char *src, int srclen, u8 *dst, int dstmax)
{
    int si = 0;
    int di = 0;

    while (si < srclen && di < dstmax) {
        if (src[si] == '\\' && si + 1 < srclen) {
            si++;
            switch (src[si]) {
                case 'n':  dst[di++] = '\n'; break;
                case 't':  dst[di++] = '\t'; break;
                case 'r':  dst[di++] = '\r'; break;
                case '0':  dst[di++] = '\0'; break;
                case '\\': dst[di++] = '\\'; break;
                case '"':  dst[di++] = '"';  break;
                default:   dst[di++] = (u8)src[si]; break;
            }
            si++;
        } else {
            dst[di++] = (u8)src[si++];
        }
    }
    return di;
}

/* ---- Condition code parsing ---- */

static int parse_cond(const char *name, int len)
{
    /* handle b.cond and cset condition names */
    if (len == 2) {
        if (strncmp(name, "eq", 2) == 0) return COND_EQ;
        if (strncmp(name, "ne", 2) == 0) return COND_NE;
        if (strncmp(name, "cs", 2) == 0) return COND_CS;
        if (strncmp(name, "hs", 2) == 0) return COND_CS;
        if (strncmp(name, "cc", 2) == 0) return COND_CC;
        if (strncmp(name, "lo", 2) == 0) return COND_CC;
        if (strncmp(name, "mi", 2) == 0) return COND_MI;
        if (strncmp(name, "pl", 2) == 0) return COND_PL;
        if (strncmp(name, "vs", 2) == 0) return COND_VS;
        if (strncmp(name, "vc", 2) == 0) return COND_VC;
        if (strncmp(name, "hi", 2) == 0) return COND_HI;
        if (strncmp(name, "ls", 2) == 0) return COND_LS;
        if (strncmp(name, "ge", 2) == 0) return COND_GE;
        if (strncmp(name, "lt", 2) == 0) return COND_LT;
        if (strncmp(name, "gt", 2) == 0) return COND_GT;
        if (strncmp(name, "le", 2) == 0) return COND_LE;
        if (strncmp(name, "al", 2) == 0) return COND_AL;
    }
    return -1;
}

/* ---- Align current section ---- */

static void align_section(struct assembler *as, int alignment)
{
    u32 off = cur_offset(as);
    u32 aligned = (off + (u32)alignment - 1) & ~((u32)alignment - 1);
    u32 al = (u32)alignment;
    struct asm_section *sec = &as->sections[as->cur_section];

    while (cur_offset(as) < aligned) {
        emit_byte(as, 0);
    }

    /* Update section alignment metadata (take maximum) */
    if (al > sec->alignment) sec->alignment = al;
}

/* ---- Conditional assembly ---- */

static int is_cond_active(struct assembler *as)
{
    int i;
    for (i = 0; i < as->cond_depth; i++) {
        if (!as->cond_stack[i]) return 0;
    }
    return 1;
}

/* ---- Macro support ---- */

static int find_macro(struct assembler *as, const char *name, int len)
{
    int i;
    int nlen;
    for (i = 0; i < as->num_macros; i++) {
        nlen = (int)strlen(as->macros[i].name);
        if (nlen == len && strncmp(as->macros[i].name, name, (size_t)len) == 0)
            return i;
    }
    return -1;
}

static void expand_macro(struct assembler *as, int macro_idx)
{
    struct asm_macro *m = &as->macros[macro_idx];
    char expanded[MAX_MACRO_BODY * 2];
    char args[MAX_MACRO_PARAMS][256];
    int nargs = 0;
    int ei = 0;
    int bi;

    /* collect arguments from current token stream until newline.
     * GAS allows both comma-separated and space-separated arguments.
     * Strategy: peek ahead to see if commas exist on this line.
     * If commas exist, split by commas (allows multi-token args).
     * If no commas, split by whitespace (each token is one arg). */
    {
        int has_commas = 0;
        const char *scan = as->lex.pos;
        while (*scan && *scan != '\n') {
            if (*scan == ',') { has_commas = 1; break; }
            scan++;
        }
        if (has_commas) {
            /* comma-separated: each arg is all tokens between commas */
            while (as->tok.kind != ASTOK_NEWLINE &&
                   as->tok.kind != ASTOK_EOF) {
                if (nargs < MAX_MACRO_PARAMS) {
                    const char *arg_start = as->tok.start;
                    const char *arg_end = as->tok.start + as->tok.len;
                    int alen;
                    while (as->tok.kind != ASTOK_COMMA &&
                           as->tok.kind != ASTOK_NEWLINE &&
                           as->tok.kind != ASTOK_EOF) {
                        arg_end = as->tok.start + as->tok.len;
                        advance(as);
                    }
                    alen = (int)(arg_end - arg_start);
                    if (alen > 255) alen = 255;
                    memcpy(args[nargs], arg_start, (size_t)alen);
                    args[nargs][alen] = '\0';
                    nargs++;
                } else {
                    while (as->tok.kind != ASTOK_COMMA &&
                           as->tok.kind != ASTOK_NEWLINE &&
                           as->tok.kind != ASTOK_EOF)
                        advance(as);
                }
                if (as->tok.kind == ASTOK_COMMA) advance(as);
            }
        } else {
            /* space-separated: each token is one argument */
            while (as->tok.kind != ASTOK_NEWLINE &&
                   as->tok.kind != ASTOK_EOF) {
                if (nargs < MAX_MACRO_PARAMS) {
                    int alen = as->tok.len;
                    if (alen > 255) alen = 255;
                    memcpy(args[nargs], as->tok.start, (size_t)alen);
                    args[nargs][alen] = '\0';
                    nargs++;
                }
                advance(as);
            }
        }
    }

    /* substitute parameters in body: \param -> arg value */
    for (bi = 0; bi < m->body_len && ei < (int)sizeof(expanded) - 128; bi++) {
        if (m->body[bi] == '\\' && bi + 1 < m->body_len) {
            /* look up parameter name */
            int pi;
            int matched = 0;
            for (pi = 0; pi < m->num_params; pi++) {
                int plen = (int)strlen(m->params[pi]);
                if (bi + 1 + plen <= m->body_len &&
                    strncmp(m->body + bi + 1, m->params[pi],
                            (size_t)plen) == 0) {
                    /* substitute */
                    if (pi < nargs) {
                        int alen = (int)strlen(args[pi]);
                        memcpy(expanded + ei, args[pi], (size_t)alen);
                        ei += alen;
                    }
                    bi += plen; /* skip param name (loop will ++ past \\) */
                    matched = 1;
                    break;
                }
            }
            if (!matched) {
                /* handle \() token pasting (GAS syntax) — skip it */
                if (bi + 1 < m->body_len && m->body[bi + 1] == '(' &&
                    bi + 2 < m->body_len && m->body[bi + 2] == ')') {
                    bi += 2; /* skip () after backslash */
                } else if (bi + 1 < m->body_len && m->body[bi + 1] == '@') {
                    /* \@ - unique per macro invocation counter */
                    int ulen;
                    char ubuf[16];
                    static int macro_uid = 0;
                    sprintf(ubuf, "%d", macro_uid++);
                    ulen = (int)strlen(ubuf);
                    memcpy(expanded + ei, ubuf, (size_t)ulen);
                    ei += ulen;
                    bi += 1; /* skip @ */
                } else {
                    expanded[ei++] = m->body[bi]; /* keep backslash */
                }
            }
        } else {
            expanded[ei++] = m->body[bi];
        }
    }
    expanded[ei] = '\0';

    /* recursively assemble the expanded text */
    {
        struct as_lexer saved_lex = as->lex;
        struct as_token saved_tok = as->tok;
        assemble_pass(as, expanded);
        as->lex = saved_lex;
        as->tok = saved_tok;
    }
}

/* ---- Register alias support ---- */

static int find_reg_alias(struct assembler *as, const char *name, int len)
{
    int i;
    int nlen;
    for (i = 0; i < as->num_reg_aliases; i++) {
        nlen = (int)strlen(as->reg_aliases[i].name);
        if (nlen == len &&
            strncmp(as->reg_aliases[i].name, name, (size_t)len) == 0)
            return i;
    }
    return -1;
}

/* ---- Skip to end of current line ---- */

static void skip_rest_of_line(struct assembler *as)
{
    while (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF) {
        advance(as);
    }
}

/* ---- Local numeric labels ---- */

static void add_local_label(struct assembler *as, int digit)
{
    struct local_label *ll;
    if (as->num_local_labels >= MAX_LOCAL_LABELS) {
        fprintf(stderr, "as: too many local labels\n");
        exit(1);
    }
    ll = &as->local_labels[as->num_local_labels++];
    ll->digit = digit;
    ll->section = as->cur_section;
    ll->value = cur_offset(as);
    ll->seq = as->local_label_seq++;
}

static int resolve_local_label(struct assembler *as, int digit, int forward)
{
    /* Build a synthetic symbol name for the resolved label.
     * For forward: find the first local_label with this digit and
     *   value > cur_offset in the same section, or same offset but higher seq.
     * For backward: find the last one with value <= cur_offset.
     */
    int i;
    int best = -1;
    u64 cur = cur_offset(as);
    char synname[32];
    int sym_idx;

    if (forward) {
        for (i = 0; i < as->num_local_labels; i++) {
            if (as->local_labels[i].digit == digit &&
                as->local_labels[i].section == as->cur_section &&
                as->local_labels[i].value >= cur) {
                if (best < 0 ||
                    as->local_labels[i].seq < as->local_labels[best].seq)
                    best = i;
            }
        }
    } else {
        for (i = 0; i < as->num_local_labels; i++) {
            if (as->local_labels[i].digit == digit &&
                as->local_labels[i].section == as->cur_section &&
                as->local_labels[i].value <= cur) {
                if (best < 0 ||
                    as->local_labels[i].seq > as->local_labels[best].seq)
                    best = i;
            }
        }
    }

    /* Create or find a synthetic symbol for this specific instance */
    if (best >= 0) {
        sprintf(synname, ".LL%d_%d", digit, as->local_labels[best].seq);
        sym_idx = add_sym(as, synname, (int)strlen(synname));
        as->syms[sym_idx].section = as->local_labels[best].section;
        as->syms[sym_idx].value = as->local_labels[best].value;
        as->syms[sym_idx].is_defined = 1;
        return sym_idx;
    }

    /* not found -- create an undefined symbol */
    sprintf(synname, ".LL%d_undef", digit);
    return add_sym(as, synname, (int)strlen(synname));
}

/* ---- Expression evaluation for .word/.quad/.if operands ---- */
/* Supports: number, symbol, parentheses, +, -, *, /, |, &, ^, ~,
 *           <<, >>, ==, !=, . (current position) */

static i64 parse_expr_or(struct assembler *as);

static i64 parse_expr_atom(struct assembler *as)
{
    i64 val = 0;
    int sym_idx;

    if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
        val = (i64)as->tok.val;
        advance(as);
        return val;
    }

    if (as->tok.kind == ASTOK_LPAREN) {
        advance(as); /* skip ( */
        val = parse_expr_or(as);
        if (as->tok.kind == ASTOK_RPAREN)
            advance(as); /* skip ) */
        return val;
    }

    if (as->tok.kind == ASTOK_MINUS) {
        advance(as);
        return -parse_expr_atom(as);
    }

    if (as->tok.kind == ASTOK_TILDE) {
        advance(as);
        return ~parse_expr_atom(as);
    }

    if (as->tok.kind == ASTOK_IDENT && as->tok.len == 1 &&
        as->tok.start[0] == '.') {
        val = (i64)cur_offset(as);
        advance(as);
        return val;
    }

    if (as->tok.kind == ASTOK_IDENT || as->tok.kind == ASTOK_DIRECTIVE) {
        /* handle numeric label refs (e.g. 1f, 1b, 662b, 664f) */
        if (as->tok.len >= 2 && as->tok.start[0] >= '0' &&
            as->tok.start[0] <= '9' &&
            (as->tok.start[as->tok.len - 1] == 'f' ||
             as->tok.start[as->tok.len - 1] == 'b')) {
            int digit = (int)as->tok.val;
            int fwd = (as->tok.start[as->tok.len - 1] == 'f');
            sym_idx = resolve_local_label(as, digit, fwd);
        } else {
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
        }
        if (as->syms[sym_idx].is_defined) {
            val = (i64)as->syms[sym_idx].value;
        }
        advance(as);
        /* handle \() suffix (GAS token pasting — skip it) */
        return val;
    }

    return 0;
}

static i64 parse_expr_mul(struct assembler *as)
{
    i64 val = parse_expr_atom(as);
    for (;;) {
        if (as->tok.kind == ASTOK_STAR) {
            advance(as);
            val *= parse_expr_atom(as);
        } else if (as->tok.kind == ASTOK_SLASH) {
            i64 divisor;
            advance(as);
            divisor = parse_expr_atom(as);
            if (divisor != 0) val /= divisor;
        } else {
            break;
        }
    }
    return val;
}

static i64 parse_expr_shift(struct assembler *as)
{
    i64 val = parse_expr_mul(as);
    for (;;) {
        if (as->tok.kind == ASTOK_LSHIFT) {
            advance(as);
            val <<= parse_expr_mul(as);
        } else if (as->tok.kind == ASTOK_RSHIFT) {
            advance(as);
            val >>= parse_expr_mul(as);
        } else {
            break;
        }
    }
    return val;
}

static i64 parse_expr_add(struct assembler *as)
{
    i64 val = parse_expr_shift(as);
    for (;;) {
        if (as->tok.kind == ASTOK_PLUS) {
            advance(as);
            val += parse_expr_shift(as);
        } else if (as->tok.kind == ASTOK_MINUS) {
            advance(as);
            val -= parse_expr_shift(as);
        } else {
            break;
        }
    }
    return val;
}

static i64 parse_expr_and(struct assembler *as)
{
    i64 val = parse_expr_add(as);
    while (as->tok.kind == ASTOK_AMPERSAND) {
        advance(as);
        val &= parse_expr_add(as);
    }
    return val;
}

static i64 parse_expr_or(struct assembler *as)
{
    i64 val = parse_expr_and(as);
    while (as->tok.kind == ASTOK_PIPE) {
        advance(as);
        val |= parse_expr_and(as);
    }
    return val;
}

static i64 parse_expr_cmp(struct assembler *as)
{
    i64 val = parse_expr_or(as);
    if (as->tok.kind == ASTOK_EQ) {
        advance(as);
        val = (val == parse_expr_or(as)) ? 1 : 0;
    } else if (as->tok.kind == ASTOK_NE) {
        advance(as);
        val = (val != parse_expr_or(as)) ? 1 : 0;
    }
    return val;
}

static i64 parse_expr_value(struct assembler *as)
{
    return parse_expr_cmp(as);
}

/* ---- Parse section flags string ("awx", "aMS", etc.) ---- */
static u64 parse_section_flags(const char *str, int len)
{
    u64 flags = 0;
    int i;

    for (i = 0; i < len; i++) {
        switch (str[i]) {
            case 'a': flags |= SHF_ALLOC; break;
            case 'w': flags |= SHF_WRITE; break;
            case 'x': flags |= SHF_EXECINSTR; break;
            case 'M': flags |= SHF_MERGE; break;
            case 'S': flags |= SHF_STRINGS; break;
            case 'G': flags |= SHF_GROUP; break;
            default: break;
        }
    }
    return flags;
}

/* ---- Parse section type (@progbits, @nobits, @note) ---- */
static u32 parse_section_type(const char *str, int len)
{
    if (len >= 8 && strncmp(str, "progbits", 8) == 0)
        return SHT_PROGBITS;
    if (len >= 6 && strncmp(str, "nobits", 6) == 0)
        return SHT_NOBITS;
    if (len >= 4 && strncmp(str, "note", 4) == 0)
        return SHT_NOTE;
    return SHT_PROGBITS;  /* default */
}

/* ---- Resolve symbol reference (handles numeric label refs) ---- */
static int resolve_sym_ref(struct assembler *as, const char *name, int len)
{
    int idx;

    if (len >= 2 && name[0] >= '0' && name[0] <= '9' &&
        (name[len - 1] == 'f' || name[len - 1] == 'b')) {
        /* parse the numeric part (all chars except last f/b) */
        long num = 0;
        int ni;
        int all_dig = 1;
        for (ni = 0; ni < len - 1; ni++) {
            if (name[ni] < '0' || name[ni] > '9') { all_dig = 0; break; }
            num = num * 10 + (name[ni] - '0');
        }
        if (all_dig) {
            int fwd = (name[len - 1] == 'f');
            return resolve_local_label(as, (int)num, fwd);
        }
    }
    idx = add_sym(as, name, len);
    /*
     * If the symbol is not defined in this file, mark it as a global
     * undefined external so the linker can resolve it. Without this,
     * references like "bl kernel_main" would be silently dropped
     * because non-global, non-defined symbols are omitted from the
     * output symbol table.
     */
    if (!as->syms[idx].is_defined && !as->syms[idx].is_global) {
        as->syms[idx].is_global = 1;
        as->syms[idx].binding = STB_GLOBAL;
    }
    return idx;
}

/* ---- Directive parsing ---- */

static void parse_directive(struct assembler *as)
{
    const struct as_token *dir = &as->tok;
    int sym_idx;
    long count;
    u8 strbuf[1024];
    int slen;

    if (tok_eq(dir, ".global") || tok_eq(dir, ".globl")) {
        advance(as); /* consume directive */
        if (as->tok.kind == ASTOK_IDENT || as->tok.kind == ASTOK_LABEL) {
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            as->syms[sym_idx].is_global = 1;
            as->syms[sym_idx].binding = STB_GLOBAL;
            advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".text")) {
        advance(as);
        as->prev_section = as->cur_section;
        as->cur_section = SEC_TEXT;
        return;
    }

    if (tok_eq(dir, ".data")) {
        advance(as);
        as->prev_section = as->cur_section;
        as->cur_section = SEC_DATA;
        return;
    }

    if (tok_eq(dir, ".rodata")) {
        advance(as);
        as->prev_section = as->cur_section;
        as->cur_section = SEC_RODATA;
        return;
    }

    if (tok_eq(dir, ".bss")) {
        advance(as);
        as->prev_section = as->cur_section;
        as->cur_section = SEC_BSS;
        return;
    }

    if (tok_eq(dir, ".section")) {
        int sec_id;
        char sec_name[128];
        int sec_namelen = 0;
        u64 sec_flags = 0;
        u32 sec_type = SHT_PROGBITS;
        int have_flags = 0;
        int have_type = 0;

        advance(as); /* consume .section */
        /* parse section name -- can be identifier, directive, or string.
         * Compound names like .init.text or .note.GNU-stack are split
         * across multiple tokens by the lexer. We glue adjacent
         * DIRECTIVE, IDENT, and MINUS tokens into a single name. */
        if (as->tok.kind == ASTOK_DIRECTIVE || as->tok.kind == ASTOK_IDENT) {
            const char *name_start = as->tok.start;
            const char *name_end = as->tok.start + as->tok.len;
            advance(as);
            for (;;) {
                if ((as->tok.kind == ASTOK_DIRECTIVE ||
                     as->tok.kind == ASTOK_IDENT ||
                     as->tok.kind == ASTOK_MINUS) &&
                    as->tok.start == name_end) {
                    name_end = as->tok.start + as->tok.len;
                    advance(as);
                } else {
                    break;
                }
            }
            sec_namelen = (int)(name_end - name_start);
            if (sec_namelen > 127) sec_namelen = 127;
            memcpy(sec_name, name_start, (size_t)sec_namelen);
            sec_name[sec_namelen] = '\0';
        } else if (as->tok.kind == ASTOK_STRING) {
            sec_namelen = as->tok.len;
            if (sec_namelen > 127) sec_namelen = 127;
            memcpy(sec_name, as->tok.start, (size_t)sec_namelen);
            sec_name[sec_namelen] = '\0';
            advance(as);
        } else {
            skip_rest_of_line(as);
            return;
        }

        /* parse optional flags: , "awx" */
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as); /* consume comma */
            if (as->tok.kind == ASTOK_STRING) {
                sec_flags = parse_section_flags(
                    as->tok.start, as->tok.len);
                have_flags = 1;
                advance(as);
            }
        }

        /* parse optional type: , @progbits / @nobits / @note */
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as); /* consume comma */
            /* type may be @progbits or %progbits */
            if (as->tok.kind == ASTOK_IDENT ||
                as->tok.kind == ASTOK_DIRECTIVE) {
                const char *ts = as->tok.start;
                int tl = as->tok.len;
                /* skip leading @ or % */
                if (tl > 0 && (ts[0] == '@' || ts[0] == '%')) {
                    ts++;
                    tl--;
                }
                sec_type = parse_section_type(ts, tl);
                have_type = 1;
                advance(as);
            }
        }

        /* If no explicit flags, infer from name */
        if (!have_flags) {
            if (strcmp(sec_name, ".text") == 0)
                sec_flags = SHF_ALLOC | SHF_EXECINSTR;
            else if (strcmp(sec_name, ".data") == 0)
                sec_flags = SHF_ALLOC | SHF_WRITE;
            else if (strcmp(sec_name, ".bss") == 0) {
                sec_flags = SHF_ALLOC | SHF_WRITE;
                if (!have_type) sec_type = SHT_NOBITS;
            } else if (strcmp(sec_name, ".rodata") == 0)
                sec_flags = SHF_ALLOC;
            else
                sec_flags = SHF_ALLOC; /* default: allocatable */
        }
        if (strcmp(sec_name, ".bss") == 0 && !have_type)
            sec_type = SHT_NOBITS;

        sec_id = find_or_create_section(as, sec_name, sec_namelen,
                                        sec_type, sec_flags);
        as->prev_section = as->cur_section;
        as->cur_section = sec_id;

        /* skip any remaining tokens on the line */
        while (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF) {
            advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".byte")) {
        advance(as);
        while (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
            emit_byte(as, (u8)(as->tok.val & 0xFF));
            advance(as);
            if (as->tok.kind == ASTOK_COMMA) advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".hword") || tok_eq(dir, ".short")) {
        advance(as);
        for (;;) {
            if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM ||
                as->tok.kind == ASTOK_LPAREN || as->tok.kind == ASTOK_IDENT ||
                as->tok.kind == ASTOK_DIRECTIVE) {
                i64 ev = parse_expr_value(as);
                emit_u16(as, (u16)(ev & 0xFFFF));
                if (as->tok.kind == ASTOK_COMMA) { advance(as); continue; }
            }
            break;
        }
        return;
    }

    if (tok_eq(dir, ".word")) {
        advance(as);
        for (;;) {
            if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM ||
                as->tok.kind == ASTOK_IDENT ||
                as->tok.kind == ASTOK_DIRECTIVE ||
                as->tok.kind == ASTOK_LPAREN) {
                i64 ev = parse_expr_value(as);
                emit_u32(as, (u32)ev);
                if (as->tok.kind == ASTOK_COMMA) { advance(as); continue; }
            }
            break;
        }
        return;
    }

    if (tok_eq(dir, ".quad") || tok_eq(dir, ".xword")) {
        advance(as);
        if (as->tok.kind == ASTOK_IDENT &&
            as->tok.len > 1) {
            /* check if it's a simple symbol ref that needs relocation */
            /* peek: if followed by minus, it's a difference expr */
            const char *saved_pos = as->lex.pos;
            int saved_line = as->lex.line;
            struct as_token saved_next;
            struct as_token cur_tok = as->tok;
            as_next_token(&as->lex, &saved_next);
            as->lex.pos = saved_pos;
            as->lex.line = saved_line;
            if (saved_next.kind == ASTOK_MINUS) {
                /* expression: sym - sym2 */
                i64 ev = parse_expr_value(as);
                emit_u64(as, (u64)ev);
            } else {
                /* simple symbol reference -- needs relocation */
                sym_idx = add_sym(as, cur_tok.start, cur_tok.len);
                add_reloc(as, sym_idx, R_AARCH64_ABS64, 0);
                emit_u64(as, 0);
                advance(as);
            }
        } else {
            for (;;) {
                if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM ||
                    as->tok.kind == ASTOK_IDENT ||
                    as->tok.kind == ASTOK_DIRECTIVE) {
                    i64 ev = parse_expr_value(as);
                    emit_u64(as, (u64)ev);
                    if (as->tok.kind == ASTOK_COMMA) {
                        advance(as); continue;
                    }
                }
                break;
            }
        }
        return;
    }

    if (tok_eq(dir, ".ascii")) {
        advance(as);
        if (as->tok.kind == ASTOK_STRING) {
            slen = unescape_string(as->tok.start, as->tok.len,
                                   strbuf, (int)sizeof(strbuf));
            {
                int si;
                for (si = 0; si < slen; si++) {
                    emit_byte(as, strbuf[si]);
                }
            }
            advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".asciz") || tok_eq(dir, ".string")) {
        advance(as);
        if (as->tok.kind == ASTOK_STRING) {
            slen = unescape_string(as->tok.start, as->tok.len,
                                   strbuf, (int)sizeof(strbuf));
            {
                int si;
                for (si = 0; si < slen; si++) {
                    emit_byte(as, strbuf[si]);
                }
            }
            emit_byte(as, 0); /* null terminator */
            advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".align") || tok_eq(dir, ".p2align")) {
        int a;

        advance(as);
        if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
            a = (int)as->tok.val;
            /* On aarch64, .align is power-of-2 (same as .p2align) */
            a = 1 << a;
            if (a < 1) a = 1;
            align_section(as, a);
            advance(as);
        }
        /* skip optional fill/max args */
        while (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM)
                advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".zero") || tok_eq(dir, ".space")) {
        advance(as);
        if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
            count = as->tok.val;
            {
                long ci;
                for (ci = 0; ci < count; ci++) {
                    emit_byte(as, 0);
                }
            }
            advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".type")) {
        advance(as);
        if (as->tok.kind == ASTOK_IDENT) {
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            advance(as);
            if (as->tok.kind == ASTOK_COMMA) advance(as);
            /* expect %function, @function, STT_FUNC, etc. */
            if (as->tok.kind == ASTOK_IDENT || as->tok.kind == ASTOK_DIRECTIVE) {
                if (tok_eq(&as->tok, "function") ||
                    tok_eq(&as->tok, "%function") ||
                    tok_eq(&as->tok, "@function") ||
                    tok_eq(&as->tok, "STT_FUNC")) {
                    as->syms[sym_idx].sym_type = STT_FUNC;
                } else if (tok_eq(&as->tok, "object") ||
                           tok_eq(&as->tok, "%object") ||
                           tok_eq(&as->tok, "@object") ||
                           tok_eq(&as->tok, "STT_OBJECT")) {
                    as->syms[sym_idx].sym_type = STT_OBJECT;
                } else if (tok_eq(&as->tok, "notype") ||
                           tok_eq(&as->tok, "%notype") ||
                           tok_eq(&as->tok, "@notype") ||
                           tok_eq(&as->tok, "STT_NOTYPE")) {
                    as->syms[sym_idx].sym_type = STT_NOTYPE;
                }
                advance(as);
            } else if (as->tok.kind == ASTOK_IMM) {
                /* skip */
                advance(as);
            }
        }
        /* skip rest of line */
        while (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF) {
            advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".size")) {
        advance(as);
        if (as->tok.kind == ASTOK_IDENT) {
            i64 sz;
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            advance(as);
            if (as->tok.kind == ASTOK_COMMA) advance(as);
            sz = parse_expr_value(as);
            if (sz < 0) sz = -sz;
            as->syms[sym_idx].sym_size = (u64)sz;
        }
        return;
    }

    /* ---- Macro definition (.macro / .endm) ---- */
    if (tok_eq(dir, ".macro")) {
        struct asm_macro *m;
        advance(as);
        if (as->num_macros >= MAX_MACROS) {
            fprintf(stderr, "as: too many macros\n");
            exit(1);
        }
        m = &as->macros[as->num_macros];
        memset(m, 0, sizeof(*m));
        if (as->tok.kind == ASTOK_IDENT) {
            int nlen = as->tok.len;
            if (nlen > 63) nlen = 63;
            memcpy(m->name, as->tok.start, (size_t)nlen);
            m->name[nlen] = '\0';
            advance(as);
        }
        /* skip leading comma before first param (GAS allows it) */
        if (as->tok.kind == ASTOK_COMMA) advance(as);
        while (as->tok.kind == ASTOK_IDENT || as->tok.kind == ASTOK_REG) {
            int plen = as->tok.len;
            if (m->num_params < MAX_MACRO_PARAMS) {
                if (plen > 63) plen = 63;
                memcpy(m->params[m->num_params], as->tok.start, (size_t)plen);
                m->params[m->num_params][plen] = '\0';
                m->num_params++;
            }
            advance(as);
            /* skip :req, :vararg suffixes on param names */
            if (as->tok.kind == ASTOK_COLON) {
                advance(as); /* skip : */
                if (as->tok.kind == ASTOK_IDENT)
                    advance(as); /* skip req/vararg */
            }
            /* skip default value (= expr); the '=' char is silently
             * consumed by the lexer as an unknown char, so we see the
             * default value token(s) directly after the param name */
            if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
                advance(as); /* skip default value */
            }
            if (as->tok.kind == ASTOK_COMMA) advance(as);
        }
        as->in_macro_def = 1;
        as->cur_macro = as->num_macros;
        as->num_macros++;
        return;
    }

    if (tok_eq(dir, ".endm")) {
        advance(as);
        as->in_macro_def = 0;
        return;
    }

    /* ---- Conditional assembly ---- */
    if (tok_eq(dir, ".if")) {
        long cval;
        advance(as);
        cval = (long)parse_expr_value(as);
        if (as->cond_depth < MAX_COND_DEPTH) {
            as->cond_stack[as->cond_depth] = (cval != 0) ? 1 : 0;
            as->cond_met[as->cond_depth] = (cval != 0) ? 1 : 0;
            as->cond_depth++;
        }
        return;
    }

    if (tok_eq(dir, ".ifdef")) {
        int found;
        advance(as);
        found = 0;
        if (as->tok.kind == ASTOK_IDENT) {
            int si = find_sym(as, as->tok.start, as->tok.len);
            if (si >= 0 && as->syms[si].is_defined) found = 1;
            advance(as);
        }
        if (as->cond_depth < MAX_COND_DEPTH) {
            as->cond_stack[as->cond_depth] = found;
            as->cond_met[as->cond_depth] = found;
            as->cond_depth++;
        }
        return;
    }

    if (tok_eq(dir, ".ifndef")) {
        int found;
        advance(as);
        found = 0;
        if (as->tok.kind == ASTOK_IDENT) {
            int si = find_sym(as, as->tok.start, as->tok.len);
            if (si >= 0 && as->syms[si].is_defined) found = 1;
            advance(as);
        }
        if (as->cond_depth < MAX_COND_DEPTH) {
            as->cond_stack[as->cond_depth] = found ? 0 : 1;
            as->cond_met[as->cond_depth] = found ? 0 : 1;
            as->cond_depth++;
        }
        return;
    }

    /* .ifb — if blank (no argument supplied to macro param) */
    if (tok_eq(dir, ".ifb")) {
        int blank = 1;
        advance(as);
        /* If next token is on the same line and is not blank, it's not blank */
        if (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF) {
            blank = 0;
            advance(as);
        }
        if (as->cond_depth < MAX_COND_DEPTH) {
            as->cond_stack[as->cond_depth] = blank;
            as->cond_met[as->cond_depth] = blank;
            as->cond_depth++;
        }
        return;
    }

    if (tok_eq(dir, ".ifnb")) {
        int blank = 1;
        advance(as);
        if (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF) {
            blank = 0;
            advance(as);
        }
        if (as->cond_depth < MAX_COND_DEPTH) {
            as->cond_stack[as->cond_depth] = blank ? 0 : 1;
            as->cond_met[as->cond_depth] = blank ? 0 : 1;
            as->cond_depth++;
        }
        return;
    }

    if (tok_eq(dir, ".else")) {
        advance(as);
        if (as->cond_depth > 0) {
            int top = as->cond_depth - 1;
            if (as->cond_met[top])
                as->cond_stack[top] = 0;
            else {
                as->cond_stack[top] = 1;
                as->cond_met[top] = 1;
            }
        }
        return;
    }

    if (tok_eq(dir, ".endif")) {
        advance(as);
        if (as->cond_depth > 0)
            as->cond_depth--;
        return;
    }

    /* ---- Section stack ---- */
    if (tok_eq(dir, ".pushsection")) {
        int sec_id;
        char sec_name[128];
        int sec_namelen = 0;
        u64 sec_flags = 0;
        u32 sec_type = SHT_PROGBITS;
        int have_flags = 0;
        int have_type = 0;

        advance(as);
        if (as->sec_stack_top < MAX_SEC_STACK)
            as->sec_stack[as->sec_stack_top++] = as->cur_section;

        if (as->tok.kind == ASTOK_DIRECTIVE ||
            as->tok.kind == ASTOK_IDENT) {
            const char *name_start = as->tok.start;
            const char *name_end = as->tok.start + as->tok.len;
            advance(as);
            for (;;) {
                if ((as->tok.kind == ASTOK_DIRECTIVE ||
                     as->tok.kind == ASTOK_IDENT ||
                     as->tok.kind == ASTOK_MINUS) &&
                    as->tok.start == name_end) {
                    name_end = as->tok.start + as->tok.len;
                    advance(as);
                } else {
                    break;
                }
            }
            sec_namelen = (int)(name_end - name_start);
            if (sec_namelen > 127) sec_namelen = 127;
            memcpy(sec_name, name_start, (size_t)sec_namelen);
            sec_name[sec_namelen] = '\0';

            /* optional flags */
            if (as->tok.kind == ASTOK_COMMA) {
                advance(as);
                if (as->tok.kind == ASTOK_STRING) {
                    sec_flags = parse_section_flags(
                        as->tok.start, as->tok.len);
                    have_flags = 1;
                    advance(as);
                }
            }
            /* optional type */
            if (as->tok.kind == ASTOK_COMMA) {
                advance(as);
                if (as->tok.kind == ASTOK_IDENT ||
                    as->tok.kind == ASTOK_DIRECTIVE) {
                    const char *ts = as->tok.start;
                    int tl = as->tok.len;
                    if (tl > 0 && (ts[0] == '@' || ts[0] == '%')) {
                        ts++; tl--;
                    }
                    sec_type = parse_section_type(ts, tl);
                    have_type = 1;
                    advance(as);
                }
            }

            if (!have_flags) {
                if (strcmp(sec_name, ".text") == 0)
                    sec_flags = SHF_ALLOC | SHF_EXECINSTR;
                else if (strcmp(sec_name, ".data") == 0)
                    sec_flags = SHF_ALLOC | SHF_WRITE;
                else if (strcmp(sec_name, ".bss") == 0) {
                    sec_flags = SHF_ALLOC | SHF_WRITE;
                    if (!have_type) sec_type = SHT_NOBITS;
                } else if (strcmp(sec_name, ".rodata") == 0)
                    sec_flags = SHF_ALLOC;
                else
                    sec_flags = SHF_ALLOC;
            }
            if (strcmp(sec_name, ".bss") == 0 && !have_type)
                sec_type = SHT_NOBITS;

            sec_id = find_or_create_section(as, sec_name, sec_namelen,
                                            sec_type, sec_flags);
            as->prev_section = as->cur_section;
            as->cur_section = sec_id;
        }
        while (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF)
            advance(as);
        return;
    }

    if (tok_eq(dir, ".popsection")) {
        advance(as);
        if (as->sec_stack_top > 0) {
            as->prev_section = as->cur_section;
            as->cur_section = as->sec_stack[--as->sec_stack_top];
        }
        return;
    }

    if (tok_eq(dir, ".previous")) {
        int tmp;
        advance(as);
        tmp = as->cur_section;
        as->cur_section = as->prev_section;
        as->prev_section = tmp;
        return;
    }

    /* ---- Symbol directives ---- */
    if (tok_eq(dir, ".weak")) {
        advance(as);
        if (as->tok.kind == ASTOK_IDENT) {
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            as->syms[sym_idx].binding = STB_WEAK;
            as->syms[sym_idx].is_global = 1;
            advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".hidden")) {
        advance(as);
        if (as->tok.kind == ASTOK_IDENT) {
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            as->syms[sym_idx].visibility = STV_HIDDEN;
            advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".local")) {
        advance(as);
        if (as->tok.kind == ASTOK_IDENT) {
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            as->syms[sym_idx].is_global = 0;
            as->syms[sym_idx].binding = STB_LOCAL;
            advance(as);
        }
        return;
    }

    if (tok_eq(dir, ".set") || tok_eq(dir, ".equ")) {
        i64 ev;
        advance(as);
        if (as->tok.kind == ASTOK_IDENT ||
            as->tok.kind == ASTOK_DIRECTIVE) {
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            advance(as);
            if (as->tok.kind == ASTOK_COMMA) advance(as);
            ev = parse_expr_value(as);
            as->syms[sym_idx].value = (u64)ev;
            as->syms[sym_idx].section = (int)SHN_ABS;
            as->syms[sym_idx].is_defined = 1;
        }
        /* skip any remaining tokens on line */
        while (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF)
            advance(as);
        return;
    }

    /* ---- .balign (byte alignment) ---- */
    if (tok_eq(dir, ".balign")) {
        int a;
        advance(as);
        if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
            a = (int)as->tok.val;
            if (a < 1) a = 1;
            align_section(as, a);
            advance(as);
        }
        return;
    }

    /* ---- .fill count, size, value ---- */
    if (tok_eq(dir, ".fill")) {
        long fill_count, fill_size, fill_val;
        long fi;
        advance(as);
        fill_count = 0; fill_size = 1; fill_val = 0;
        if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
            fill_count = as->tok.val;
            advance(as);
        }
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
                fill_size = as->tok.val;
                advance(as);
            }
        }
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
                fill_val = as->tok.val;
                advance(as);
            }
        }
        for (fi = 0; fi < fill_count; fi++) {
            long bsi;
            for (bsi = 0; bsi < fill_size; bsi++) {
                emit_byte(as, (u8)((fill_val >> (bsi * 8)) & 0xFF));
            }
        }
        return;
    }

    /* ---- .rept count / .endr ---- */
    if (tok_eq(dir, ".rept")) {
        advance(as);
        as->rept_count = (int)parse_expr_value(as);
        if (as->rept_count < 0) as->rept_count = 0;
        as->in_rept = 1;
        as->rept_body_len = 0;
        return;
    }

    if (tok_eq(dir, ".endr")) {
        advance(as);
        return;
    }

    /* ---- .inst WORD — emit raw 32-bit instruction ---- */
    if (tok_eq(dir, ".inst")) {
        advance(as);
        for (;;) {
            if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM ||
                as->tok.kind == ASTOK_LPAREN || as->tok.kind == ASTOK_IDENT ||
                as->tok.kind == ASTOK_DIRECTIVE) {
                i64 inst_val = parse_expr_value(as);
                emit_u32(as, (u32)inst_val);
                if (as->tok.kind == ASTOK_COMMA) { advance(as); continue; }
            }
            break;
        }
        return;
    }

    /* ---- .purgem macro_name — undefine a macro ---- */
    if (tok_eq(dir, ".purgem")) {
        advance(as);
        if (as->tok.kind == ASTOK_IDENT) {
            int mi = find_macro(as, as->tok.start, as->tok.len);
            if (mi >= 0) {
                /* remove by shifting remaining macros down */
                int mj;
                for (mj = mi; mj < as->num_macros - 1; mj++) {
                    as->macros[mj] = as->macros[mj + 1];
                }
                as->num_macros--;
            }
            advance(as);
        }
        return;
    }

    /* ---- .req / .unreq — register aliases (handled in assemble_pass) ---- */
    if (tok_eq(dir, ".req")) {
        /* .req is normally preceded by an ident (name .req xN)
         * which is handled in assemble_pass; if we get here, skip */
        advance(as);
        skip_rest_of_line(as);
        return;
    }

    if (tok_eq(dir, ".unreq")) {
        advance(as);
        if (as->tok.kind == ASTOK_IDENT) {
            int ai = find_reg_alias(as, as->tok.start, as->tok.len);
            if (ai >= 0) {
                int aj;
                for (aj = ai; aj < as->num_reg_aliases - 1; aj++) {
                    as->reg_aliases[aj] = as->reg_aliases[aj + 1];
                }
                as->num_reg_aliases--;
            }
            advance(as);
        }
        return;
    }

    /* ---- .irp var, val1, val2, ... / .endr ---- */
    if (tok_eq(dir, ".irp")) {
        int vlen;
        advance(as);
        /* parse variable name */
        if (as->tok.kind == ASTOK_IDENT || as->tok.kind == ASTOK_REG) {
            vlen = as->tok.len;
            if (vlen > 63) vlen = 63;
            memcpy(as->irp_var, as->tok.start, (size_t)vlen);
            as->irp_var[vlen] = '\0';
            advance(as);
        } else {
            as->irp_var[0] = '\0';
        }
        if (as->tok.kind == ASTOK_COMMA) advance(as);
        /* collect values until end of line */
        as->irp_num_values = 0;
        while (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF) {
            if (as->irp_num_values < MAX_IRP_VALUES) {
                int alen = as->tok.len;
                if (alen > 127) alen = 127;
                memcpy(as->irp_values[as->irp_num_values],
                       as->tok.start, (size_t)alen);
                as->irp_values[as->irp_num_values][alen] = '\0';
                as->irp_num_values++;
            }
            advance(as);
            if (as->tok.kind == ASTOK_COMMA) advance(as);
        }
        as->in_irp = 1;
        as->irp_body_len = 0;
        return;
    }

    /* ---- .irpc var, string / .endr ---- */
    if (tok_eq(dir, ".irpc")) {
        int vlen;
        advance(as);
        /* parse variable name */
        if (as->tok.kind == ASTOK_IDENT || as->tok.kind == ASTOK_REG) {
            vlen = as->tok.len;
            if (vlen > 63) vlen = 63;
            memcpy(as->irp_var, as->tok.start, (size_t)vlen);
            as->irp_var[vlen] = '\0';
            advance(as);
        } else {
            as->irp_var[0] = '\0';
        }
        if (as->tok.kind == ASTOK_COMMA) advance(as);
        /* collect the character string */
        as->irp_num_chars = 0;
        if (as->tok.kind == ASTOK_STRING) {
            int ci;
            for (ci = 0; ci < as->tok.len && ci < 255; ci++) {
                as->irp_chars[as->irp_num_chars++] = as->tok.start[ci];
            }
            advance(as);
        } else if (as->tok.kind == ASTOK_IDENT ||
                   as->tok.kind == ASTOK_NUM) {
            /* bare token: iterate over each character */
            int ci;
            for (ci = 0; ci < as->tok.len && ci < 255; ci++) {
                as->irp_chars[as->irp_num_chars++] = as->tok.start[ci];
            }
            advance(as);
        }
        as->irp_chars[as->irp_num_chars] = '\0';
        as->in_irp = 2;
        as->irp_body_len = 0;
        return;
    }

    /* ---- .arch — accept and ignore ---- */
    if (tok_eq(dir, ".arch")) {
        advance(as);
        skip_rest_of_line(as);
        return;
    }

    /* ---- .incbin "filename" [,skip[,count]] ---- */
    if (tok_eq(dir, ".incbin")) {
        advance(as);
        if (as->tok.kind == ASTOK_STRING) {
            char fname[256];
            int flen = as->tok.len;
            long skip_bytes = 0;
            long count_bytes = -1;
            FILE *incf;

            if (flen > 255) flen = 255;
            memcpy(fname, as->tok.start, (size_t)flen);
            fname[flen] = '\0';
            advance(as);

            /* optional skip */
            if (as->tok.kind == ASTOK_COMMA) {
                advance(as);
                if (as->tok.kind == ASTOK_NUM ||
                    as->tok.kind == ASTOK_IMM) {
                    skip_bytes = as->tok.val;
                    advance(as);
                }
            }
            /* optional count */
            if (as->tok.kind == ASTOK_COMMA) {
                advance(as);
                if (as->tok.kind == ASTOK_NUM ||
                    as->tok.kind == ASTOK_IMM) {
                    count_bytes = as->tok.val;
                    advance(as);
                }
            }

            incf = fopen(fname, "rb");
            if (incf) {
                int ch;
                long pos = 0;
                long emitted = 0;
                if (skip_bytes > 0)
                    fseek(incf, skip_bytes, SEEK_SET);
                while ((ch = fgetc(incf)) != EOF) {
                    if (count_bytes >= 0 && emitted >= count_bytes)
                        break;
                    emit_byte(as, (u8)ch);
                    emitted++;
                    pos++;
                }
                fclose(incf);
            } else if (as->pass == 2) {
                fprintf(stderr, "as: line %d: cannot open incbin file: %s\n",
                        as->tok.line, fname);
            }
        }
        return;
    }

    /* ---- CFI directives — parse and skip (no .eh_frame emission) ---- */
    if (tok_eq(dir, ".cfi_startproc") ||
        tok_eq(dir, ".cfi_endproc") ||
        tok_eq(dir, ".cfi_def_cfa") ||
        tok_eq(dir, ".cfi_def_cfa_register") ||
        tok_eq(dir, ".cfi_def_cfa_offset") ||
        tok_eq(dir, ".cfi_offset") ||
        tok_eq(dir, ".cfi_adjust_cfa_offset") ||
        tok_eq(dir, ".cfi_restore") ||
        tok_eq(dir, ".cfi_remember_state") ||
        tok_eq(dir, ".cfi_restore_state") ||
        tok_eq(dir, ".cfi_undefined") ||
        tok_eq(dir, ".cfi_same_value") ||
        tok_eq(dir, ".cfi_rel_offset") ||
        tok_eq(dir, ".cfi_signal_frame") ||
        tok_eq(dir, ".cfi_return_column") ||
        tok_eq(dir, ".cfi_personality") ||
        tok_eq(dir, ".cfi_lsda") ||
        tok_eq(dir, ".cfi_escape") ||
        tok_eq(dir, ".cfi_val_offset") ||
        tok_eq(dir, ".cfi_sections") ||
        tok_eq(dir, ".cfi_window_save")) {
        advance(as);
        skip_rest_of_line(as);
        return;
    }

    /* ---- .ltorg - emit literal pool (currently a no-op placeholder) ---- */
    if (tok_eq(dir, ".ltorg") || tok_eq(dir, ".pool")) {
        advance(as);
        /* align to 4 bytes */
        align_section(as, 4);
        return;
    }

    /* ---- .subsection N — switch to subsection (treat like .text) ---- */
    if (tok_eq(dir, ".subsection")) {
        advance(as);
        /* skip subsection number */
        if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM)
            advance(as);
        return;
    }

    /* ---- .org expr — set location counter ---- */
    if (tok_eq(dir, ".org")) {
        i64 target;
        u32 cur;
        advance(as);
        target = parse_expr_value(as);
        cur = cur_offset(as);
        /* only pad forward */
        if ((u64)target > cur) {
            u32 pad = (u32)target - cur;
            u32 pi;
            for (pi = 0; pi < pad; pi++)
                emit_byte(as, 0);
        }
        return;
    }

    /* ---- .long (alias for .word — 4-byte value) ---- */
    if (tok_eq(dir, ".long")) {
        advance(as);
        for (;;) {
            if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM ||
                as->tok.kind == ASTOK_IDENT ||
                as->tok.kind == ASTOK_DIRECTIVE) {
                i64 ev = parse_expr_value(as);
                emit_u32(as, (u32)ev);
                if (as->tok.kind == ASTOK_COMMA) { advance(as); continue; }
            }
            break;
        }
        return;
    }

    /* ---- .arch_extension — accept and skip ---- */
    if (tok_eq(dir, ".arch_extension")) {
        advance(as);
        skip_rest_of_line(as);
        return;
    }

    /* ---- .nops count — emit count NOPs ---- */
    if (tok_eq(dir, ".nops")) {
        long nop_count;
        long ni;
        advance(as);
        nop_count = 0;
        if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
            nop_count = as->tok.val;
            advance(as);
        }
        for (ni = 0; ni < nop_count; ni++) {
            emit_insn(as, a64_nop());
        }
        return;
    }

    /* ---- .protected / .internal — symbol visibility ---- */
    if (tok_eq(dir, ".protected") || tok_eq(dir, ".internal")) {
        advance(as);
        skip_rest_of_line(as);
        return;
    }

    /* unknown directive -- skip to end of line */
    advance(as);
    while (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF) {
        advance(as);
    }
}

/* ---- Instruction parsing and encoding ---- */

/* Mnemonic IDs */
enum mnemonic {
    MN_NONE = 0,
    MN_MOV, MN_MOVZ, MN_MOVK, MN_MOVN, MN_MVN,
    MN_ADD, MN_SUB, MN_SUBS, MN_MUL, MN_SDIV, MN_UDIV, MN_MSUB,
    MN_AND, MN_ORR, MN_EOR, MN_BIC,
    MN_LSL, MN_LSR, MN_ASR,
    MN_CMP, MN_CMN,
    MN_CSET,
    MN_B, MN_BL, MN_BR, MN_BLR, MN_RET,
    MN_B_EQ, MN_B_NE, MN_B_CS, MN_B_CC, MN_B_MI, MN_B_PL,
    MN_B_VS, MN_B_VC, MN_B_HI, MN_B_LS, MN_B_GE, MN_B_LT,
    MN_B_GT, MN_B_LE, MN_B_AL,
    MN_LDR, MN_STR, MN_LDRB, MN_STRB, MN_LDRH, MN_STRH,
    MN_LDRSW,
    MN_STP, MN_LDP,
    MN_ADRP, MN_ADR,
    MN_NEG,
    MN_SVC, MN_NOP,
    MN_SXTB, MN_SXTH, MN_SXTW,
    MN_UXTB, MN_UXTH,
    MN_CBZ, MN_CBNZ,
    /* FP instructions */
    MN_FADD, MN_FSUB, MN_FMUL, MN_FDIV, MN_FNEG,
    MN_FCMP, MN_FMOV, MN_FCVT,
    MN_SCVTF, MN_UCVTF, MN_FCVTZS,
    /* Atomic/Exclusive */
    MN_LDXR, MN_STXR, MN_LDXRB, MN_LDXRH, MN_STXRB, MN_STXRH,
    MN_LDAXR, MN_STLXR, MN_LDAXRB, MN_LDAXRH, MN_STLXRB, MN_STLXRH,
    MN_LDADD, MN_STADD, MN_SWP, MN_CAS, MN_CASP,
    /* Barriers */
    MN_DMB, MN_DSB, MN_ISB,
    /* System registers */
    MN_MRS, MN_MSR,
    /* Exception/System */
    MN_ERET, MN_HVC, MN_SMC,
    MN_WFE, MN_WFI, MN_YIELD, MN_SEV, MN_SEVL,
    /* Conditional select */
    MN_CSEL, MN_CSINC, MN_CSINV, MN_CSNEG,
    /* Test and branch */
    MN_TBNZ, MN_TBZ,
    /* Bit manipulation */
    MN_CLZ, MN_RBIT, MN_REV, MN_REV16, MN_REV32,
    /* Bitfield */
    MN_UBFM, MN_SBFM, MN_BFM,
    MN_BFI, MN_BFXIL, MN_UBFX, MN_SBFX,
    /* BTI/PAC */
    MN_BTI, MN_PACIASP, MN_AUTIASP,
    /* System instructions (dc, ic, tlbi, at, hint, clrex, sys) */
    MN_DC, MN_IC, MN_TLBI, MN_AT, MN_HINT, MN_CLREX, MN_SYS,
    /* Load-acquire / Store-release */
    MN_LDAR, MN_LDARB, MN_LDARH,
    MN_STLR, MN_STLRB, MN_STLRH,
    /* Load-acquire RCpc */
    MN_LDAPR, MN_LDAPRB, MN_LDAPRH,
    /* Conditional compare */
    MN_CCMP, MN_CCMN,
    /* Extract */
    MN_EXTR,
    /* Prefetch */
    MN_PRFM,
    /* Signed byte/halfword load to 64-bit */
    MN_LDRSB, MN_LDRSH,
    /* Multiply-accumulate */
    MN_MADD, MN_SMADDL, MN_UMADDL,
    /* Logical with flags / additional logical */
    MN_TST, MN_ANDS, MN_BICS, MN_ORN,
    /* Non-temporal pair loads/stores */
    MN_STNP, MN_LDNP,
    /* Adds with flags */
    MN_ADDS,
    /* 32-bit variants for shifts */
    MN_LSL_I, MN_LSR_I, MN_ASR_I
};

struct mnemonic_entry {
    const char *name;
    int id;
};

static const struct mnemonic_entry mnemonics[] = {
    {"mov",   MN_MOV},   {"movz",  MN_MOVZ},  {"movk",  MN_MOVK},
    {"movn",  MN_MOVN},  {"mvn",   MN_MVN},
    {"add",   MN_ADD},   {"sub",   MN_SUB},   {"subs",  MN_SUBS},
    {"mul",   MN_MUL},   {"sdiv",  MN_SDIV},  {"udiv",  MN_UDIV},
    {"msub",  MN_MSUB},
    {"and",   MN_AND},   {"orr",   MN_ORR},   {"eor",   MN_EOR},
    {"bic",   MN_BIC},
    {"lsl",   MN_LSL},   {"lsr",   MN_LSR},   {"asr",   MN_ASR},
    {"cmp",   MN_CMP},   {"cmn",   MN_CMN},   {"cset",  MN_CSET},
    {"b",     MN_B},     {"bl",    MN_BL},    {"br",    MN_BR},
    {"blr",   MN_BLR},   {"ret",   MN_RET},
    {"b.eq",  MN_B_EQ},  {"b.ne",  MN_B_NE},
    {"b.cs",  MN_B_CS},  {"b.hs",  MN_B_CS},
    {"b.cc",  MN_B_CC},  {"b.lo",  MN_B_CC},
    {"b.mi",  MN_B_MI},  {"b.pl",  MN_B_PL},
    {"b.vs",  MN_B_VS},  {"b.vc",  MN_B_VC},
    {"b.hi",  MN_B_HI},  {"b.ls",  MN_B_LS},
    {"b.ge",  MN_B_GE},  {"b.lt",  MN_B_LT},
    {"b.gt",  MN_B_GT},  {"b.le",  MN_B_LE},
    {"b.al",  MN_B_AL},
    {"ldr",   MN_LDR},   {"str",   MN_STR},
    {"ldrb",  MN_LDRB},  {"strb",  MN_STRB},
    {"ldrh",  MN_LDRH},  {"strh",  MN_STRH},
    {"ldrsw", MN_LDRSW},
    {"stp",   MN_STP},   {"ldp",   MN_LDP},
    {"adrp",  MN_ADRP},  {"adr",   MN_ADR},
    {"neg",   MN_NEG},
    {"svc",   MN_SVC},   {"nop",   MN_NOP},
    {"sxtb",  MN_SXTB},  {"sxth",  MN_SXTH},  {"sxtw",  MN_SXTW},
    {"uxtb",  MN_UXTB},  {"uxth",  MN_UXTH},
    {"cbz",   MN_CBZ},   {"cbnz",  MN_CBNZ},
    /* FP instructions */
    {"fadd",  MN_FADD},  {"fsub",  MN_FSUB},
    {"fmul",  MN_FMUL},  {"fdiv",  MN_FDIV},
    {"fneg",  MN_FNEG},  {"fcmp",  MN_FCMP},
    {"fmov",  MN_FMOV},  {"fcvt",  MN_FCVT},
    {"scvtf", MN_SCVTF}, {"ucvtf", MN_UCVTF},
    {"fcvtzs", MN_FCVTZS},
    /* Atomic/Exclusive */
    {"ldxr",  MN_LDXR},  {"stxr",  MN_STXR},
    {"ldaxr", MN_LDAXR}, {"stlxr", MN_STLXR},
    {"ldadd", MN_LDADD}, {"stadd", MN_STADD},
    {"swp",   MN_SWP},   {"cas",   MN_CAS},
    /* Barriers */
    {"dmb",   MN_DMB},   {"dsb",   MN_DSB},   {"isb",   MN_ISB},
    /* System registers */
    {"mrs",   MN_MRS},   {"msr",   MN_MSR},
    /* Exception/System */
    {"eret",  MN_ERET},  {"hvc",   MN_HVC},   {"smc",   MN_SMC},
    {"wfe",   MN_WFE},   {"wfi",   MN_WFI},
    {"yield", MN_YIELD}, {"sev",   MN_SEV},   {"sevl",  MN_SEVL},
    /* Conditional select */
    {"csel",  MN_CSEL},  {"csinc", MN_CSINC},
    {"csinv", MN_CSINV}, {"csneg", MN_CSNEG},
    /* Test and branch */
    {"tbnz",  MN_TBNZ},  {"tbz",   MN_TBZ},
    /* Bit manipulation */
    {"clz",   MN_CLZ},   {"rbit",  MN_RBIT},
    {"rev",   MN_REV},   {"rev16", MN_REV16}, {"rev32", MN_REV32},
    /* Bitfield */
    {"ubfm",  MN_UBFM},  {"sbfm",  MN_SBFM},  {"bfm",   MN_BFM},
    {"bfi",   MN_BFI},   {"bfxil", MN_BFXIL},
    {"ubfx",  MN_UBFX},  {"sbfx",  MN_SBFX},
    /* BTI/PAC */
    {"bti",   MN_BTI},   {"paciasp", MN_PACIASP}, {"autiasp", MN_AUTIASP},
    /* System instructions */
    {"dc",    MN_DC},    {"ic",    MN_IC},    {"tlbi",  MN_TLBI},
    {"at",    MN_AT},    {"hint",  MN_HINT},  {"clrex", MN_CLREX},
    {"sys",   MN_SYS},
    /* Load-acquire / Store-release */
    {"ldar",  MN_LDAR},  {"ldarb", MN_LDARB}, {"ldarh", MN_LDARH},
    {"stlr",  MN_STLR},  {"stlrb", MN_STLRB}, {"stlrh", MN_STLRH},
    /* Conditional compare */
    {"ccmp",  MN_CCMP},  {"ccmn",  MN_CCMN},
    /* Extract */
    {"extr",  MN_EXTR},
    /* Prefetch */
    {"prfm",  MN_PRFM},
    /* Signed loads */
    {"ldrsb", MN_LDRSB}, {"ldrsh", MN_LDRSH},
    /* Multiply-accumulate */
    {"madd",  MN_MADD},  {"smaddl", MN_SMADDL}, {"umaddl", MN_UMADDL},
    /* Logical with flags */
    {"tst",   MN_TST},   {"ands",  MN_ANDS},  {"bics",  MN_BICS},
    {"orn",   MN_ORN},
    /* Non-temporal pair */
    {"stnp",  MN_STNP},  {"ldnp",  MN_LDNP},
    /* Adds with flags */
    {"adds",  MN_ADDS},
    {NULL, 0}
};

static int lookup_mnemonic(const char *name, int len)
{
    int i;
    int nlen;

    for (i = 0; mnemonics[i].name != NULL; i++) {
        nlen = (int)strlen(mnemonics[i].name);
        if (nlen == len && strncmp(mnemonics[i].name, name, (size_t)len) == 0) {
            return mnemonics[i].id;
        }
    }
    return MN_NONE;
}

static int cond_for_bcond(int mn)
{
    switch (mn) {
        case MN_B_EQ: return COND_EQ;
        case MN_B_NE: return COND_NE;
        case MN_B_CS: return COND_CS;
        case MN_B_CC: return COND_CC;
        case MN_B_MI: return COND_MI;
        case MN_B_PL: return COND_PL;
        case MN_B_VS: return COND_VS;
        case MN_B_VC: return COND_VC;
        case MN_B_HI: return COND_HI;
        case MN_B_LS: return COND_LS;
        case MN_B_GE: return COND_GE;
        case MN_B_LT: return COND_LT;
        case MN_B_GT: return COND_GT;
        case MN_B_LE: return COND_LE;
        case MN_B_AL: return COND_AL;
    }
    return -1;
}

/* ---- Barrier option parsing ---- */

static int __attribute__((unused)) parse_barrier_option(const char *name, int len)
{
    if (len == 2) {
        if (strncmp(name, "sy", 2) == 0) return BARRIER_SY;
        if (strncmp(name, "st", 2) == 0) return BARRIER_ST;
        if (strncmp(name, "ld", 2) == 0) return BARRIER_LD;
    }
    if (len == 3) {
        if (strncmp(name, "ish", 3) == 0) return BARRIER_ISH;
        if (strncmp(name, "nsh", 3) == 0) return BARRIER_NSH;
        if (strncmp(name, "osh", 3) == 0) return BARRIER_OSH;
    }
    if (len == 5) {
        if (strncmp(name, "ishld", 5) == 0) return BARRIER_ISHLD;
        if (strncmp(name, "ishst", 5) == 0) return BARRIER_ISHST;
        if (strncmp(name, "nshld", 5) == 0) return BARRIER_NSHLD;
        if (strncmp(name, "nshst", 5) == 0) return BARRIER_NSHST;
        if (strncmp(name, "oshld", 5) == 0) return BARRIER_OSHLD;
        if (strncmp(name, "oshst", 5) == 0) return BARRIER_OSHST;
    }
    return -1;
}

/* ---- System register name parsing ---- */

struct sysreg_entry {
    const char *name;
    u32 encoding;
};

static const struct sysreg_entry sysregs[] = {
    {"sctlr_el1",   SYSREG_SCTLR_EL1},
    {"ttbr0_el1",   SYSREG_TTBR0_EL1},
    {"ttbr1_el1",   SYSREG_TTBR1_EL1},
    {"tcr_el1",     SYSREG_TCR_EL1},
    {"mair_el1",    SYSREG_MAIR_EL1},
    {"vbar_el1",    SYSREG_VBAR_EL1},
    {"currentel",   SYSREG_CURRENTEL},
    {"daif",        SYSREG_DAIF},
    {"nzcv",        SYSREG_NZCV},
    {"fpcr",        SYSREG_FPCR},
    {"fpsr",        SYSREG_FPSR},
    {"tpidr_el0",   SYSREG_TPIDR_EL0},
    {"tpidr_el1",   SYSREG_TPIDR_EL1},
    {"cntfrq_el0",  SYSREG_CNTFRQ_EL0},
    {"cntvct_el0",  SYSREG_CNTVCT_EL0},
    {"sp_el0",      SYSREG_SP_EL0},
    {"elr_el1",     SYSREG_ELR_EL1},
    {"spsr_el1",    SYSREG_SPSR_EL1},
    {NULL, 0}
};

static int __attribute__((unused)) parse_sysreg(const char *name, int len, u32 *enc)
{
    int i, nlen;

    for (i = 0; sysregs[i].name != NULL; i++) {
        nlen = (int)strlen(sysregs[i].name);
        if (nlen == len && strncmp(sysregs[i].name, name, (size_t)len) == 0) {
            *enc = sysregs[i].encoding;
            return 1;
        }
    }
    return 0;
}

static void parse_instruction(struct assembler *as)
{
    int mn;
    int rd, rn, rm, ra;
    int is_wreg_d, is_wreg_n;
    long imm;
    int sym_idx;
    int cond;
    int line;

    mn = lookup_mnemonic(as->tok.start, as->tok.len);
    line = as->tok.line;
    advance(as); /* consume mnemonic */

    switch (mn) {

    /* ---- RET, NOP ---- */
    case MN_RET:
        emit_insn(as, a64_ret());
        break;

    case MN_NOP:
        emit_insn(as, a64_nop());
        break;

    /* ---- SVC ---- */
    case MN_SVC:
        if (as->tok.kind == ASTOK_IMM) {
            imm = as->tok.val;
            advance(as);
        } else {
            imm = 0;
        }
        emit_insn(as, a64_svc((u32)imm));
        break;

    /* ---- MOV Xd, Xm / MOV Xd, #imm ---- */
    case MN_MOV:
        rd = (int)as->tok.val;
        is_wreg_d = as->tok.is_wreg;
        advance(as); /* rd */
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_REG) {
            rm = (int)as->tok.val;
            advance(as);
            /* sp uses ADD encoding (ORR treats r31 as xzr) */
            if (rd == REG_SP || rm == REG_SP)
                emit_insn(as, a64_add_i(rd, rm, 0));
            else
                emit_insn(as, a64_mov(rd, rm));
        } else if (as->tok.kind == ASTOK_IMM) {
            imm = as->tok.val;
            advance(as);
            if (imm >= 0 && imm <= 0xFFFF) {
                emit_insn(as, is_wreg_d
                    ? a64_movz_w(rd, (u16)imm, 0)
                    : a64_movz(rd, (u16)imm, 0));
            } else if (imm < 0 && imm >= -0x10000) {
                emit_insn(as, is_wreg_d
                    ? a64_movn_w(rd, (u16)(~imm), 0)
                    : a64_movn(rd, (u16)(~imm), 0));
            } else {
                /* large immediate: use movz + movk sequence */
                emit_insn(as, is_wreg_d
                    ? a64_movz_w(rd, (u16)(imm & 0xFFFF), 0)
                    : a64_movz(rd, (u16)(imm & 0xFFFF), 0));
                if (imm & 0xFFFF0000L) {
                    emit_insn(as, is_wreg_d
                        ? a64_movk_w(rd,
                            (u16)((imm >> 16) & 0xFFFF), 16)
                        : a64_movk(rd,
                            (u16)((imm >> 16) & 0xFFFF), 16));
                }
                if ((u64)imm & 0xFFFF00000000UL) {
                    emit_insn(as, a64_movk(rd,
                        (u16)((imm >> 32) & 0xFFFF), 32));
                }
                if ((u64)imm & 0xFFFF000000000000UL) {
                    emit_insn(as, a64_movk(rd,
                        (u16)((imm >> 48) & 0xFFFF), 48));
                }
            }
        }
        break;

    /* ---- MOVZ, MOVK, MOVN ---- */
    case MN_MOVZ:
    case MN_MOVK:
    case MN_MOVN:
    {
        int shift = 0;

        rd = (int)as->tok.val;
        is_wreg_d = as->tok.is_wreg;
        advance(as); /* rd */
        expect(as, ASTOK_COMMA);
        imm = as->tok.val;
        advance(as); /* imm */
        /* optional LSL #N */
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_IDENT && tok_eq(&as->tok, "lsl")) {
                advance(as);
                if (as->tok.kind == ASTOK_IMM) {
                    shift = (int)as->tok.val;
                    advance(as);
                }
            } else if (as->tok.kind == ASTOK_IDENT &&
                       tok_eq(&as->tok, "LSL")) {
                advance(as);
                if (as->tok.kind == ASTOK_IMM) {
                    shift = (int)as->tok.val;
                    advance(as);
                }
            }
        }
        if (mn == MN_MOVZ)
            emit_insn(as, is_wreg_d
                ? a64_movz_w(rd, (u16)(imm & 0xFFFF), shift)
                : a64_movz(rd, (u16)(imm & 0xFFFF), shift));
        else if (mn == MN_MOVK)
            emit_insn(as, is_wreg_d
                ? a64_movk_w(rd, (u16)(imm & 0xFFFF), shift)
                : a64_movk(rd, (u16)(imm & 0xFFFF), shift));
        else
            emit_insn(as, is_wreg_d
                ? a64_movn_w(rd, (u16)(imm & 0xFFFF), shift)
                : a64_movn(rd, (u16)(imm & 0xFFFF), shift));
        break;
    }

    /* ---- MVN ---- */
    case MN_MVN:
        rd = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val;
        advance(as);
        emit_insn(as, a64_mvn(rd, rm));
        break;

    /* ---- ADD, SUB, SUBS ---- */
    case MN_ADD:
    case MN_SUB:
    case MN_SUBS:
        rd = (int)as->tok.val;
        is_wreg_d = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_REG) {
            int shift_type = 0; /* 0=LSL, 1=LSR, 2=ASR */
            int shift_amt = 0;
            u32 base;
            rm = (int)as->tok.val;
            advance(as);
            /* check for optional shift: , lsl/lsr/asr #N */
            if (as->tok.kind == ASTOK_COMMA) {
                const char *saved_p = as->lex.pos;
                int saved_l = as->lex.line;
                struct as_token peek;
                as_next_token(&as->lex, &peek);
                as->lex.pos = saved_p;
                as->lex.line = saved_l;
                if (peek.kind == ASTOK_IDENT) {
                    advance(as); /* consume comma */
                    if (tok_eq(&as->tok, "lsl") || tok_eq(&as->tok, "LSL"))
                        shift_type = 0;
                    else if (tok_eq(&as->tok, "lsr") || tok_eq(&as->tok, "LSR"))
                        shift_type = 1;
                    else if (tok_eq(&as->tok, "asr") || tok_eq(&as->tok, "ASR"))
                        shift_type = 2;
                    advance(as); /* consume shift name */
                    if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
                        shift_amt = (int)as->tok.val;
                        advance(as);
                    }
                }
            }
            if (shift_amt == 0 && shift_type == 0) {
                if (mn == MN_ADD)
                    emit_insn(as, a64_add_r(rd, rn, rm));
                else if (mn == MN_SUB)
                    emit_insn(as, a64_sub_r(rd, rn, rm));
                else
                    emit_insn(as, a64_subs_r(rd, rn, rm));
            } else {
                /* shifted register: ADD/SUB Xd, Xn, Xm, shift #amt
                 * ADD:  sf_0_0_01011_sh_0_Rm_imm6_Rn_Rd
                 * SUB:  sf_1_0_01011_sh_0_Rm_imm6_Rn_Rd
                 * SUBS: sf_1_1_01011_sh_0_Rm_imm6_Rn_Rd */
                if (is_wreg_d) {
                    base = (u32)0x0B000000;
                } else {
                    base = (u32)0x8B000000;
                }
                if (mn == MN_SUB)
                    base |= (u32)0x40000000;
                else if (mn == MN_SUBS)
                    base |= (u32)0x60000000;
                emit_insn(as, base
                    | (((u32)shift_type & 3) << 22)
                    | (((u32)rm & 0x1F) << 16)
                    | (((u32)shift_amt & 0x3F) << 10)
                    | (((u32)rn & 0x1F) << 5)
                    | ((u32)rd & 0x1F));
            }
        } else if (as->tok.kind == ASTOK_IMM) {
            imm = as->tok.val;
            advance(as);
            if (mn == MN_ADD)
                emit_insn(as, a64_add_i(rd, rn, (u32)imm));
            else if (mn == MN_SUB)
                emit_insn(as, a64_sub_i(rd, rn, (u32)imm));
            else
                emit_insn(as, a64_subs_i(rd, rn, (u32)imm));
        } else if (as->tok.kind == ASTOK_COLON && mn == MN_ADD) {
            /* :lo12:symbol - ADD with page-offset relocation */
            advance(as); /* skip first : */
            advance(as); /* skip 'lo12:' (lexed as label) */
            /* symbol name (may be IDENT or DIRECTIVE for .Lxx) */
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            add_reloc(as, sym_idx, R_AARCH64_ADD_ABS_LO12_NC, 0);
            advance(as);
            emit_insn(as, a64_add_i(rd, rn, 0));
        }
        break;

    /* ---- MUL, SDIV, UDIV ---- */
    case MN_MUL:
    case MN_SDIV:
    case MN_UDIV:
        rd = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val;
        advance(as);
        if (mn == MN_MUL)
            emit_insn(as, a64_mul(rd, rn, rm));
        else if (mn == MN_SDIV)
            emit_insn(as, a64_sdiv(rd, rn, rm));
        else
            emit_insn(as, a64_udiv(rd, rn, rm));
        break;

    /* ---- MSUB ---- */
    case MN_MSUB:
        rd = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        ra = (int)as->tok.val;
        advance(as);
        emit_insn(as, a64_msub(rd, rn, rm, ra));
        break;

    /* ---- AND, ORR, EOR ---- */
    case MN_AND:
    case MN_ORR:
    case MN_EOR:
    case MN_BIC:
    {
        int shift_type = 0;
        int shift_amt = 0;
        u32 log_base;

        rd = (int)as->tok.val;
        is_wreg_d = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            imm = as->tok.val;
            advance(as);
            if (mn == MN_AND)
                emit_insn(as, a64_and_i(rd, rn, (u64)imm));
            else if (mn == MN_ORR)
                emit_insn(as, a64_orr_i(rd, rn, (u64)imm));
            else if (mn == MN_EOR)
                emit_insn(as, a64_eor_i(rd, rn, (u64)imm));
            else /* BIC */
                emit_insn(as, a64_and_i(rd, rn, ~(u64)imm));
        } else {
            rm = (int)as->tok.val;
            advance(as);
            /* check for optional shift: , lsl/lsr/asr #N */
            if (as->tok.kind == ASTOK_COMMA) {
                const char *sp = as->lex.pos;
                int sl = as->lex.line;
                struct as_token pk;
                as_next_token(&as->lex, &pk);
                as->lex.pos = sp;
                as->lex.line = sl;
                if (pk.kind == ASTOK_IDENT) {
                    advance(as); /* consume comma */
                    if (tok_eq(&as->tok, "lsl") || tok_eq(&as->tok, "LSL"))
                        shift_type = 0;
                    else if (tok_eq(&as->tok, "lsr") || tok_eq(&as->tok, "LSR"))
                        shift_type = 1;
                    else if (tok_eq(&as->tok, "asr") || tok_eq(&as->tok, "ASR"))
                        shift_type = 2;
                    else if (tok_eq(&as->tok, "ror") || tok_eq(&as->tok, "ROR"))
                        shift_type = 3;
                    advance(as);
                    if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
                        shift_amt = (int)as->tok.val;
                        advance(as);
                    }
                }
            }
            if (shift_amt == 0 && shift_type == 0) {
                if (mn == MN_AND)
                    emit_insn(as, a64_and_r(rd, rn, rm));
                else if (mn == MN_ORR)
                    emit_insn(as, a64_orr_r(rd, rn, rm));
                else if (mn == MN_EOR)
                    emit_insn(as, a64_eor_r(rd, rn, rm));
                else /* BIC */
                    emit_insn(as, (u32)0x8A200000
                        | (((u32)rm & 0x1F) << 16)
                        | (((u32)rn & 0x1F) << 5)
                        | ((u32)rd & 0x1F));
            } else {
                /* Logical shifted register:
                 * AND:  sf_00_01010_sh_0_Rm_imm6_Rn_Rd
                 * BIC:  sf_00_01010_sh_1_Rm_imm6_Rn_Rd
                 * ORR:  sf_01_01010_sh_0_Rm_imm6_Rn_Rd
                 * ORN:  sf_01_01010_sh_1_Rm_imm6_Rn_Rd
                 * EOR:  sf_10_01010_sh_0_Rm_imm6_Rn_Rd
                 * EON:  sf_10_01010_sh_1_Rm_imm6_Rn_Rd */
                log_base = is_wreg_d ? (u32)0x0A000000 : (u32)0x8A000000;
                if (mn == MN_ORR) log_base |= (u32)0x20000000;
                else if (mn == MN_EOR) log_base |= (u32)0x40000000;
                if (mn == MN_BIC) log_base |= (u32)0x00200000;
                emit_insn(as, log_base
                    | (((u32)shift_type & 3) << 22)
                    | (((u32)rm & 0x1F) << 16)
                    | (((u32)shift_amt & 0x3F) << 10)
                    | (((u32)rn & 0x1F) << 5)
                    | ((u32)rd & 0x1F));
            }
        }
        break;
    }

    /* ---- LSL, LSR, ASR ---- */
    case MN_LSL:
    case MN_LSR:
    case MN_ASR:
        rd = (int)as->tok.val;
        is_wreg_d = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            /* immediate shift — encode as UBFM/SBFM alias */
            int shift_amt = (int)as->tok.val;
            int width = is_wreg_d ? 31 : 63;
            advance(as);
            if (mn == MN_LSL) {
                /* LSL Xd, Xn, #n = UBFM Xd, Xn, #(-n MOD width+1), #(width-n) */
                int immr = (-shift_amt) & width;
                int imms = width - shift_amt;
                emit_insn(as, a64_ubfm(rd, rn, immr, imms));
            } else if (mn == MN_LSR) {
                /* LSR Xd, Xn, #n = UBFM Xd, Xn, #n, #width */
                emit_insn(as, a64_ubfm(rd, rn, shift_amt, width));
            } else {
                /* ASR Xd, Xn, #n = SBFM Xd, Xn, #n, #width */
                emit_insn(as, a64_sbfm(rd, rn, shift_amt, width));
            }
        } else {
            /* register shift */
            rm = (int)as->tok.val;
            advance(as);
            if (mn == MN_LSL)
                emit_insn(as, a64_lsl(rd, rn, rm));
            else if (mn == MN_LSR)
                emit_insn(as, a64_lsr(rd, rn, rm));
            else
                emit_insn(as, a64_asr(rd, rn, rm));
        }
        break;

    /* ---- CMP ---- */
    case MN_CMP:
    {
        int cmp_is_w;
        rn = (int)as->tok.val;
        cmp_is_w = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_REG) {
            int cmp_shift_type = 0, cmp_shift_amt = 0;
            rm = (int)as->tok.val;
            advance(as);
            /* check for optional shift */
            if (as->tok.kind == ASTOK_COMMA) {
                const char *sp = as->lex.pos;
                int sl = as->lex.line;
                struct as_token pk;
                as_next_token(&as->lex, &pk);
                as->lex.pos = sp;
                as->lex.line = sl;
                if (pk.kind == ASTOK_IDENT) {
                    advance(as);
                    if (tok_eq(&as->tok, "lsl") || tok_eq(&as->tok, "LSL"))
                        cmp_shift_type = 0;
                    else if (tok_eq(&as->tok, "lsr") || tok_eq(&as->tok, "LSR"))
                        cmp_shift_type = 1;
                    else if (tok_eq(&as->tok, "asr") || tok_eq(&as->tok, "ASR"))
                        cmp_shift_type = 2;
                    advance(as);
                    if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
                        cmp_shift_amt = (int)as->tok.val;
                        advance(as);
                    }
                }
            }
            if (cmp_shift_amt == 0 && cmp_shift_type == 0) {
                emit_insn(as, a64_cmp_r(rn, rm));
            } else {
                /* CMP = SUBS XZR, Rn, Rm, shift #amt */
                u32 cmp_base = cmp_is_w ? (u32)0x6B000000 : (u32)0xEB000000;
                emit_insn(as, cmp_base
                    | (((u32)cmp_shift_type & 3) << 22)
                    | (((u32)rm & 0x1F) << 16)
                    | (((u32)cmp_shift_amt & 0x3F) << 10)
                    | (((u32)rn & 0x1F) << 5)
                    | (u32)REG_XZR);
            }
        } else if (as->tok.kind == ASTOK_IMM) {
            imm = as->tok.val;
            advance(as);
            emit_insn(as, a64_cmp_i(rn, (u32)imm));
        }
        break;
    }

    /* ---- CMN ---- */
    case MN_CMN:
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_IMM) {
            /* CMN Xn, #imm => ADDS XZR, Xn, #imm */
            imm = as->tok.val;
            advance(as);
            emit_insn(as, a64_add_i(REG_XZR, rn, (u32)imm));
        } else if (as->tok.kind == ASTOK_REG) {
            rm = (int)as->tok.val;
            advance(as);
            emit_insn(as, a64_add_r(REG_XZR, rn, rm));
        }
        break;

    /* ---- CSET ---- */
    case MN_CSET:
        rd = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        cond = parse_cond(as->tok.start, as->tok.len);
        if (cond < 0) {
            fprintf(stderr, "as: line %d: invalid condition code\n", line);
            exit(1);
        }
        advance(as);
        emit_insn(as, a64_cset(rd, cond));
        break;

    /* ---- NEG ---- */
    case MN_NEG:
    {
        int neg_shift_type = 0;
        int neg_shift_amt = 0;
        rd = (int)as->tok.val;
        is_wreg_d = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val;
        advance(as);
        /* check for optional shift: , lsl/lsr/asr #N */
        if (as->tok.kind == ASTOK_COMMA) {
            const char *sp = as->lex.pos;
            int sl = as->lex.line;
            struct as_token pk;
            as_next_token(&as->lex, &pk);
            as->lex.pos = sp;
            as->lex.line = sl;
            if (pk.kind == ASTOK_IDENT) {
                advance(as);
                if (tok_eq(&as->tok, "lsl") || tok_eq(&as->tok, "LSL"))
                    neg_shift_type = 0;
                else if (tok_eq(&as->tok, "lsr") || tok_eq(&as->tok, "LSR"))
                    neg_shift_type = 1;
                else if (tok_eq(&as->tok, "asr") || tok_eq(&as->tok, "ASR"))
                    neg_shift_type = 2;
                advance(as);
                if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
                    neg_shift_amt = (int)as->tok.val;
                    advance(as);
                }
            }
        }
        if (neg_shift_amt == 0 && neg_shift_type == 0) {
            emit_insn(as, a64_neg(rd, rm));
        } else {
            /* NEG Xd, Xm, shift #amt = SUB Xd, XZR, Xm, shift #amt */
            u32 base_v = is_wreg_d ? (u32)0x4B000000 : (u32)0xCB000000;
            emit_insn(as, base_v
                | (((u32)neg_shift_type & 3) << 22)
                | (((u32)rm & 0x1F) << 16)
                | (((u32)neg_shift_amt & 0x3F) << 10)
                | (((u32)REG_XZR & 0x1F) << 5)
                | ((u32)rd & 0x1F));
        }
        break;
    }

    /* ---- B, BL ---- */
    case MN_B:
    case MN_BL:
        if (as->tok.kind == ASTOK_IDENT) {
            /* branch to symbol -- needs relocation */
            sym_idx = resolve_sym_ref(as, as->tok.start, as->tok.len);
            if (mn == MN_B)
                add_reloc(as, sym_idx, R_AARCH64_JUMP26, 0);
            else
                add_reloc(as, sym_idx, R_AARCH64_CALL26, 0);
            advance(as);
            if (mn == MN_B)
                emit_insn(as, a64_b(0));
            else
                emit_insn(as, a64_bl(0));
        } else if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            imm = as->tok.val;
            advance(as);
            if (mn == MN_B)
                emit_insn(as, a64_b((i32)imm));
            else
                emit_insn(as, a64_bl((i32)imm));
        }
        break;

    /* ---- B.cond ---- */
    case MN_B_EQ: case MN_B_NE: case MN_B_CS: case MN_B_CC:
    case MN_B_MI: case MN_B_PL: case MN_B_VS: case MN_B_VC:
    case MN_B_HI: case MN_B_LS: case MN_B_GE: case MN_B_LT:
    case MN_B_GT: case MN_B_LE: case MN_B_AL:
        cond = cond_for_bcond(mn);
        if (as->tok.kind == ASTOK_IDENT) {
            sym_idx = resolve_sym_ref(as, as->tok.start, as->tok.len);
            /* conditional branches use 19-bit immediate, no standard reloc
             * -- store as JUMP26 and the linker/resolve pass handles it */
            add_reloc(as, sym_idx, R_AARCH64_JUMP26, 0);
            advance(as);
            emit_insn(as, a64_b_cond(cond, 0));
        } else if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            imm = as->tok.val;
            advance(as);
            emit_insn(as, a64_b_cond(cond, (i32)imm));
        }
        break;

    /* ---- BR, BLR ---- */
    case MN_BR:
        rn = (int)as->tok.val;
        advance(as);
        emit_insn(as, a64_br(rn));
        break;

    case MN_BLR:
        rn = (int)as->tok.val;
        advance(as);
        emit_insn(as, a64_blr(rn));
        break;

    /* ---- CBZ, CBNZ ---- */
    case MN_CBZ:
    case MN_CBNZ:
    {
        u32 base;

        rd = (int)as->tok.val;
        is_wreg_d = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);

        /* CBZ:  sf_110010_0_imm19_Rt   (sf=1 for 64-bit) */
        /* CBNZ: sf_110010_1_imm19_Rt   */
        base = is_wreg_d ? (u32)0x34000000 : (u32)0xB4000000;
        if (mn == MN_CBNZ) {
            base |= (u32)0x01000000;
        }

        if (as->tok.kind == ASTOK_IDENT) {
            sym_idx = resolve_sym_ref(as, as->tok.start, as->tok.len);
            add_reloc(as, sym_idx, R_AARCH64_JUMP26, 0);
            advance(as);
            emit_insn(as, base | ((u32)rd & 0x1F));
        } else if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            imm = as->tok.val;
            advance(as);
            emit_insn(as, base
                | (((u32)imm & 0x7FFFF) << 5)
                | ((u32)rd & 0x1F));
        }
        break;
    }

    /* ---- LDR, STR, LDRB, STRB, LDRH, STRH, LDRSW ---- */
    case MN_LDR:
    case MN_STR:
    case MN_LDRB:
    case MN_STRB:
    case MN_LDRH:
    case MN_STRH:
    case MN_LDRSW:
    {
        int rt_val;
        int is_w;
        int is_fpreg;
        int is_sreg_fp;
        int is_pre_idx = 0;
        int is_post_idx = 0;
        int is_reg_off = 0;
        int rm_off = 0;

        (void)is_pre_idx;
        (void)is_post_idx;
        (void)is_reg_off;
        (void)rm_off;

        is_fpreg = (as->tok.kind == ASTOK_FPREG);
        is_sreg_fp = as->tok.is_sreg;
        rt_val = (int)as->tok.val;
        is_w = as->tok.is_wreg;
        advance(as); /* rt */
        expect(as, ASTOK_COMMA);

        if (as->tok.kind == ASTOK_LBRACKET) {
            /* [Xn, #offset] or [Xn] or [Xn, Xm] */
            advance(as); /* [ */
            rn = (int)as->tok.val;
            advance(as); /* base register */

            imm = 0;
            sym_idx = -1;
            if (as->tok.kind == ASTOK_COMMA) {
                advance(as); /* , */
                if (as->tok.kind == ASTOK_IMM) {
                    imm = as->tok.val;
                    advance(as);
                } else if (as->tok.kind == ASTOK_REG) {
                    /* register offset: [Xn, Xm] or [Xn, Xm, lsl #N] */
                    rm_off = (int)as->tok.val;
                    is_reg_off = 1;
                    advance(as);
                    /* consume optional shift: , lsl/sxtw/uxtw #N */
                    if (as->tok.kind == ASTOK_COMMA) {
                        advance(as);
                        if (as->tok.kind == ASTOK_IDENT) {
                            advance(as); /* skip lsl/sxtw/uxtw */
                            if (as->tok.kind == ASTOK_IMM ||
                                as->tok.kind == ASTOK_NUM)
                                advance(as); /* skip shift amount */
                        }
                    }
                } else if (as->tok.kind == ASTOK_COLON) {
                    /* :lo12:symbol inside brackets */
                    advance(as); /* skip first : */
                    advance(as); /* skip 'lo12:' (lexed as label) */
                    sym_idx = add_sym(as, as->tok.start, as->tok.len);
                    advance(as); /* consume symbol */
                }
            }
            if (as->tok.kind == ASTOK_RBRACKET) {
                advance(as); /* ] */
            }
            /* check for ! (pre-index) */
            if (as->tok.kind == ASTOK_EXCL) {
                is_pre_idx = 1;
                advance(as);
            }
            /* check for post-index: ], #off */
            if (as->tok.kind == ASTOK_COMMA) {
                advance(as);
                if (as->tok.kind == ASTOK_IMM) {
                    imm = as->tok.val;
                    is_post_idx = 1;
                    advance(as);
                }
            }

            /* If we have a :lo12: relocation, emit with reloc */
            if (sym_idx >= 0) {
                if (is_fpreg) {
                    if (is_sreg_fp) {
                        add_reloc(as, sym_idx, R_AARCH64_LDST32_ABS_LO12_NC, 0);
                        emit_insn(as, (mn == MN_LDR) ?
                            a64_ldr_s(rt_val, rn, 0) :
                            a64_str_s(rt_val, rn, 0));
                    } else {
                        add_reloc(as, sym_idx, R_AARCH64_LDST64_ABS_LO12_NC, 0);
                        emit_insn(as, (mn == MN_LDR) ?
                            a64_ldr_d(rt_val, rn, 0) :
                            a64_str_d(rt_val, rn, 0));
                    }
                } else {
                    if (is_w) {
                        add_reloc(as, sym_idx, R_AARCH64_LDST32_ABS_LO12_NC, 0);
                        emit_insn(as, (mn == MN_LDR) ?
                            a64_ldr_w(rt_val, rn, 0) :
                            a64_str_w(rt_val, rn, 0));
                    } else {
                        add_reloc(as, sym_idx, R_AARCH64_LDST64_ABS_LO12_NC, 0);
                        emit_insn(as, (mn == MN_LDR) ?
                            a64_ldr(rt_val, rn, 0) :
                            a64_str(rt_val, rn, 0));
                    }
                }
                break;
            }

            /* Handle pre-index, post-index, register offset */
            if (is_reg_off && (mn == MN_LDR || mn == MN_STR)
                && !is_fpreg && !is_w) {
                if (mn == MN_LDR)
                    emit_insn(as, a64_ldr_r(rt_val, rn, rm_off));
                else
                    emit_insn(as, a64_str_r(rt_val, rn, rm_off));
            } else if (is_pre_idx
                && (mn == MN_LDR || mn == MN_STR)
                && !is_fpreg && !is_w) {
                if (mn == MN_LDR)
                    emit_insn(as, a64_ldr_pre(rt_val, rn, (i32)imm));
                else
                    emit_insn(as, a64_str_pre(rt_val, rn, (i32)imm));
            } else if (is_post_idx
                && (mn == MN_LDR || mn == MN_STR)
                && !is_fpreg && !is_w) {
                if (mn == MN_LDR)
                    emit_insn(as, a64_ldr_post(rt_val, rn, (i32)imm));
                else
                    emit_insn(as, a64_str_post(rt_val, rn, (i32)imm));
            } else if (is_pre_idx && is_fpreg && !is_sreg_fp) {
                if (mn == MN_LDR)
                    emit_insn(as, a64_ldr_d_pre(rt_val, rn, (i32)imm));
                else
                    emit_insn(as, a64_str_d_pre(rt_val, rn, (i32)imm));
            } else if (is_post_idx && is_fpreg && !is_sreg_fp) {
                if (mn == MN_LDR)
                    emit_insn(as, a64_ldr_d_post(rt_val, rn, (i32)imm));
                else
                    emit_insn(as, a64_str_d_post(rt_val, rn, (i32)imm));
            } else if (mn == MN_LDR && is_fpreg) {
                if (is_sreg_fp)
                    emit_insn(as, a64_ldr_s(rt_val, rn, (i32)imm));
                else
                    emit_insn(as, a64_ldr_d(rt_val, rn, (i32)imm));
            } else if (mn == MN_STR && is_fpreg) {
                if (is_sreg_fp)
                    emit_insn(as, a64_str_s(rt_val, rn, (i32)imm));
                else
                    emit_insn(as, a64_str_d(rt_val, rn, (i32)imm));
            } else if (mn == MN_LDR) {
                if (is_w)
                    emit_insn(as, a64_ldr_w(rt_val, rn, (i32)imm));
                else
                    emit_insn(as, a64_ldr(rt_val, rn, (i32)imm));
            } else if (mn == MN_STR) {
                if (is_w)
                    emit_insn(as, a64_str_w(rt_val, rn, (i32)imm));
                else
                    emit_insn(as, a64_str(rt_val, rn, (i32)imm));
            } else if (mn == MN_LDRB) {
                emit_insn(as, a64_ldrb(rt_val, rn, (i32)imm));
            } else if (mn == MN_STRB) {
                emit_insn(as, a64_strb(rt_val, rn, (i32)imm));
            } else if (mn == MN_LDRH) {
                emit_insn(as, a64_ldrh(rt_val, rn, (i32)imm));
            } else if (mn == MN_STRH) {
                emit_insn(as, a64_strh(rt_val, rn, (i32)imm));
            } else if (mn == MN_LDRSW) {
                emit_insn(as, a64_ldrsw(rt_val, rn, (i32)imm));
            }
        } else if (as->tok.kind == ASTOK_IDENT && mn == MN_LDR) {
            /* LDR Xt, =symbol  (pseudo-instruction via ADRP+ADD pattern) */
            /* For relocatable objects, emit ADRP + LDR with relocations */
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            add_reloc(as, sym_idx, R_AARCH64_ADR_PREL_PG_HI21, 0);
            emit_insn(as, a64_adrp(rt_val, 0));
            add_reloc(as, sym_idx, R_AARCH64_LDST64_ABS_LO12_NC, 0);
            emit_insn(as, a64_ldr(rt_val, rt_val, 0));
            advance(as);
        }
        break;
    }

    /* ---- STP, LDP ---- */
    case MN_STP:
    case MN_LDP:
    {
        int rt1_val, rt2_val;
        int is_pre = 0;
        int is_post = 0;

        rt1_val = (int)as->tok.val;
        advance(as); /* rt1 */
        expect(as, ASTOK_COMMA);
        rt2_val = (int)as->tok.val;
        advance(as); /* rt2 */
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val;
        advance(as); /* base register */

        imm = 0;
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_IMM) {
                imm = as->tok.val;
                advance(as);
            }
        }

        if (as->tok.kind == ASTOK_RBRACKET) {
            advance(as); /* ] */
            if (as->tok.kind == ASTOK_EXCL) {
                is_pre = 1;
                advance(as);
            } else if (as->tok.kind == ASTOK_COMMA) {
                /* post-index: ], #offset */
                is_post = 1;
                advance(as);
                if (as->tok.kind == ASTOK_IMM) {
                    imm = as->tok.val;
                    advance(as);
                }
            }
        }

        if (mn == MN_STP) {
            if (is_pre)
                emit_insn(as, a64_stp_pre(rt1_val, rt2_val, rn, (i32)imm));
            else if (is_post)
                emit_insn(as, a64_stp_post(rt1_val, rt2_val, rn, (i32)imm));
            else
                emit_insn(as, a64_stp(rt1_val, rt2_val, rn, (i32)imm));
        } else {
            if (is_pre)
                emit_insn(as, a64_ldp_pre(rt1_val, rt2_val, rn, (i32)imm));
            else if (is_post)
                emit_insn(as, a64_ldp_post(rt1_val, rt2_val, rn, (i32)imm));
            else
                emit_insn(as, a64_ldp(rt1_val, rt2_val, rn, (i32)imm));
        }
        break;
    }

    /* ---- ADRP, ADR ---- */
    case MN_ADRP:
    case MN_ADR:
        rd = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_IDENT || as->tok.kind == ASTOK_DIRECTIVE) {
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            if (mn == MN_ADRP) {
                add_reloc(as, sym_idx, R_AARCH64_ADR_PREL_PG_HI21, 0);
                emit_insn(as, a64_adrp(rd, 0));
            } else {
                /* ADR uses a byte-offset PC-relative relocation */
                add_reloc(as, sym_idx, R_AARCH64_ADR_PREL_PG_HI21, 0);
                emit_insn(as, a64_adr(rd, 0));
            }
            advance(as);
        } else if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            imm = as->tok.val;
            advance(as);
            if (mn == MN_ADRP)
                emit_insn(as, a64_adrp(rd, (i32)imm));
            else
                emit_insn(as, a64_adr(rd, (i32)imm));
        }
        break;

    /* ---- Sign/Zero extend ---- */
    case MN_SXTB:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_sxtb(rd, rn));
        break;

    case MN_SXTH:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_sxth(rd, rn));
        break;

    case MN_SXTW:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_sxtw(rd, rn));
        break;

    case MN_UXTB:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_uxtb(rd, rn));
        break;

    case MN_UXTH:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_uxth(rd, rn));
        break;

    /* ---- FP arithmetic: FADD, FSUB, FMUL, FDIV ---- */
    case MN_FADD:
    case MN_FSUB:
    case MN_FMUL:
    case MN_FDIV:
    {
        int is_s;

        rd = (int)as->tok.val;
        is_s = as->tok.is_sreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val; advance(as);
        if (is_s) {
            if (mn == MN_FADD) emit_insn(as, a64_fadd_s(rd, rn, rm));
            else if (mn == MN_FSUB) emit_insn(as, a64_fsub_s(rd, rn, rm));
            else if (mn == MN_FMUL) emit_insn(as, a64_fmul_s(rd, rn, rm));
            else emit_insn(as, a64_fdiv_s(rd, rn, rm));
        } else {
            if (mn == MN_FADD) emit_insn(as, a64_fadd_d(rd, rn, rm));
            else if (mn == MN_FSUB) emit_insn(as, a64_fsub_d(rd, rn, rm));
            else if (mn == MN_FMUL) emit_insn(as, a64_fmul_d(rd, rn, rm));
            else emit_insn(as, a64_fdiv_d(rd, rn, rm));
        }
        break;
    }

    /* ---- FNEG ---- */
    case MN_FNEG:
    {
        int is_s;

        rd = (int)as->tok.val;
        is_s = as->tok.is_sreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        if (is_s)
            emit_insn(as, a64_fneg_s(rd, rn));
        else
            emit_insn(as, a64_fneg_d(rd, rn));
        break;
    }

    /* ---- FCMP ---- */
    case MN_FCMP:
    {
        int is_s;

        rn = (int)as->tok.val;
        is_s = as->tok.is_sreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val; advance(as);
        if (is_s)
            emit_insn(as, a64_fcmp_s(rn, rm));
        else
            emit_insn(as, a64_fcmp_d(rn, rm));
        break;
    }

    /* ---- FMOV ---- */
    case MN_FMOV:
    {
        int dst_fp, src_fp, is_s;

        dst_fp = (as->tok.kind == ASTOK_FPREG);
        rd = (int)as->tok.val;
        is_s = as->tok.is_sreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        src_fp = (as->tok.kind == ASTOK_FPREG);
        rn = (int)as->tok.val;
        advance(as);
        if (dst_fp && src_fp) {
            if (is_s)
                emit_insn(as, a64_fmov_s(rd, rn));
            else
                emit_insn(as, a64_fmov_d(rd, rn));
        } else if (dst_fp && !src_fp) {
            emit_insn(as, a64_fmov_d_x(rd, rn));
        } else {
            emit_insn(as, a64_fmov_x_d(rd, rn));
        }
        break;
    }

    /* ---- FCVT (float<->double) ---- */
    case MN_FCVT:
    {
        int dst_s;

        rd = (int)as->tok.val;
        dst_s = as->tok.is_sreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        if (dst_s)
            emit_insn(as, a64_fcvt_sd(rd, rn));
        else
            emit_insn(as, a64_fcvt_ds(rd, rn));
        break;
    }

    /* ---- SCVTF (int->float) ---- */
    case MN_SCVTF:
    {
        int is_s;

        rd = (int)as->tok.val;
        is_s = as->tok.is_sreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        if (is_s)
            emit_insn(as, a64_scvtf_s(rd, rn));
        else
            emit_insn(as, a64_scvtf_d(rd, rn));
        break;
    }

    /* ---- UCVTF (unsigned int->float) ---- */
    case MN_UCVTF:
    {
        int is_s;

        rd = (int)as->tok.val;
        is_s = as->tok.is_sreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        if (is_s)
            emit_insn(as, a64_ucvtf_s(rd, rn));
        else
            emit_insn(as, a64_ucvtf_d(rd, rn));
        break;
    }

    /* ---- FCVTZS (float->int) ---- */
    case MN_FCVTZS:
    {
        int src_s;

        rd = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val;
        src_s = as->tok.is_sreg;
        advance(as);
        if (src_s)
            emit_insn(as, a64_fcvtzs_ws(rd, rn));
        else
            emit_insn(as, a64_fcvtzs_xd(rd, rn));
        break;
    }

    /* ---- Atomic/Exclusive ---- */
    case MN_LDXR:
    {
        int is_w_ld;
        rd = (int)as->tok.val;
        is_w_ld = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        if (is_w_ld)
            emit_insn(as, a64_ldxr_w(rd, rn));
        else
            emit_insn(as, a64_ldxr(rd, rn));
        break;
    }

    case MN_STXR:
    {
        int rs_val;
        int is_w_st;
        rs_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rd = (int)as->tok.val;
        is_w_st = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        if (is_w_st)
            emit_insn(as, a64_stxr_w(rs_val, rd, rn));
        else
            emit_insn(as, a64_stxr(rs_val, rd, rn));
        break;
    }

    case MN_LDAXR:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_ldaxr(rd, rn));
        break;

    case MN_STLXR:
    {
        int rs_val;
        rs_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_stlxr(rs_val, rd, rn));
        break;
    }

    case MN_LDADD:
    {
        int rs_val;
        rs_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_ldadd(rs_val, rd, rn));
        break;
    }

    case MN_STADD:
    {
        int rs_val;
        rs_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_stadd(rs_val, rn));
        break;
    }

    case MN_SWP:
    {
        int rs_val;
        rs_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_swp(rs_val, rd, rn));
        break;
    }

    case MN_CAS:
    {
        int rs_val;
        rs_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_cas(rs_val, rd, rn));
        break;
    }

    /* ---- Barriers ---- */
    case MN_DMB:
    {
        int opt = BARRIER_SY;
        if (as->tok.kind == ASTOK_IDENT) {
            opt = parse_barrier_option(as->tok.start, as->tok.len);
            if (opt < 0) opt = BARRIER_SY;
            advance(as);
        } else if (as->tok.kind == ASTOK_IMM ||
                   as->tok.kind == ASTOK_NUM) {
            opt = (int)as->tok.val;
            advance(as);
        }
        emit_insn(as, a64_dmb(opt));
        break;
    }

    case MN_DSB:
    {
        int opt = BARRIER_SY;
        if (as->tok.kind == ASTOK_IDENT) {
            opt = parse_barrier_option(as->tok.start, as->tok.len);
            if (opt < 0) opt = BARRIER_SY;
            advance(as);
        } else if (as->tok.kind == ASTOK_IMM ||
                   as->tok.kind == ASTOK_NUM) {
            opt = (int)as->tok.val;
            advance(as);
        }
        emit_insn(as, a64_dsb(opt));
        break;
    }

    case MN_ISB:
        emit_insn(as, a64_isb());
        break;

    /* ---- System registers ---- */
    case MN_MRS:
    {
        u32 sysreg_enc = 0;
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_IDENT) {
            parse_sysreg(as->tok.start, as->tok.len, &sysreg_enc);
            advance(as);
        }
        emit_insn(as, a64_mrs(rd, sysreg_enc));
        break;
    }

    case MN_MSR:
    {
        u32 sysreg_enc = 0;
        if (as->tok.kind == ASTOK_IDENT) {
            /* Check for MSR immediate form: daifset, daifclr, spsel */
            if (tok_eq(&as->tok, "daifset")) {
                advance(as);
                expect(as, ASTOK_COMMA);
                imm = as->tok.val; advance(as);
                emit_insn(as, a64_msr_imm(0, (int)imm));
                break;
            } else if (tok_eq(&as->tok, "daifclr")) {
                advance(as);
                expect(as, ASTOK_COMMA);
                imm = as->tok.val; advance(as);
                emit_insn(as, a64_msr_imm(1, (int)imm));
                break;
            } else if (tok_eq(&as->tok, "spsel")) {
                advance(as);
                expect(as, ASTOK_COMMA);
                imm = as->tok.val; advance(as);
                emit_insn(as, a64_msr_imm(2, (int)imm));
                break;
            }
            parse_sysreg(as->tok.start, as->tok.len, &sysreg_enc);
            advance(as);
        }
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_msr(sysreg_enc, rn));
        break;
    }

    /* ---- Exception/System ---- */
    case MN_ERET:
        emit_insn(as, a64_eret());
        break;

    case MN_HVC:
        imm = 0;
        if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            imm = as->tok.val;
            advance(as);
        }
        emit_insn(as, a64_hvc((u32)imm));
        break;

    case MN_SMC:
        imm = 0;
        if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            imm = as->tok.val;
            advance(as);
        }
        emit_insn(as, a64_smc((u32)imm));
        break;

    case MN_WFE:
        emit_insn(as, a64_wfe());
        break;

    case MN_WFI:
        emit_insn(as, a64_wfi());
        break;

    case MN_YIELD:
        emit_insn(as, a64_yield());
        break;

    case MN_SEV:
        emit_insn(as, a64_sev());
        break;

    case MN_SEVL:
        emit_insn(as, a64_sevl());
        break;

    /* ---- Conditional select ---- */
    case MN_CSEL:
    case MN_CSINC:
    case MN_CSINV:
    case MN_CSNEG:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        cond = parse_cond(as->tok.start, as->tok.len);
        if (cond < 0) {
            fprintf(stderr, "as: line %d: invalid condition\n", line);
            exit(1);
        }
        advance(as);
        if (mn == MN_CSEL)
            emit_insn(as, a64_csel(rd, rn, rm, cond));
        else if (mn == MN_CSINC)
            emit_insn(as, a64_csinc(rd, rn, rm, cond));
        else if (mn == MN_CSINV)
            emit_insn(as, a64_csinv(rd, rn, rm, cond));
        else
            emit_insn(as, a64_csneg(rd, rn, rm, cond));
        break;

    /* ---- Test and branch ---- */
    case MN_TBNZ:
    case MN_TBZ:
    {
        int bit;
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        bit = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_IDENT) {
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            add_reloc(as, sym_idx, R_AARCH64_JUMP26, 0);
            advance(as);
            if (mn == MN_TBNZ)
                emit_insn(as, a64_tbnz(rd, bit, 0));
            else
                emit_insn(as, a64_tbz(rd, bit, 0));
        } else {
            imm = as->tok.val; advance(as);
            if (mn == MN_TBNZ)
                emit_insn(as, a64_tbnz(rd, bit, (i32)imm));
            else
                emit_insn(as, a64_tbz(rd, bit, (i32)imm));
        }
        break;
    }

    /* ---- Bit manipulation ---- */
    case MN_CLZ:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_clz(rd, rn));
        break;

    case MN_RBIT:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_rbit(rd, rn));
        break;

    case MN_REV:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_rev(rd, rn));
        break;

    case MN_REV16:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_rev16(rd, rn));
        break;

    case MN_REV32:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        emit_insn(as, a64_rev32(rd, rn));
        break;

    /* ---- Bitfield ---- */
    case MN_UBFM:
    case MN_SBFM:
    case MN_BFM:
    {
        int immr_val, imms_val;
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        immr_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        imms_val = (int)as->tok.val; advance(as);
        if (mn == MN_UBFM)
            emit_insn(as, a64_ubfm(rd, rn, immr_val, imms_val));
        else if (mn == MN_SBFM)
            emit_insn(as, a64_sbfm(rd, rn, immr_val, imms_val));
        else
            emit_insn(as, a64_bfm(rd, rn, immr_val, imms_val));
        break;
    }

    /* BFI Xd,Xn,#lsb,#w => BFM Xd,Xn,#(-lsb%64),#(w-1) */
    case MN_BFI:
    {
        int lsb_val, width_val;
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        lsb_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        width_val = (int)as->tok.val; advance(as);
        emit_insn(as, a64_bfm(rd, rn,
            (-lsb_val) & 63, width_val - 1));
        break;
    }

    /* BFXIL Xd,Xn,#lsb,#w => BFM Xd,Xn,#lsb,#(lsb+w-1) */
    case MN_BFXIL:
    {
        int lsb_val, width_val;
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        lsb_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        width_val = (int)as->tok.val; advance(as);
        emit_insn(as, a64_bfm(rd, rn,
            lsb_val, lsb_val + width_val - 1));
        break;
    }

    /* UBFX Xd,Xn,#lsb,#w => UBFM Xd,Xn,#lsb,#(lsb+w-1) */
    case MN_UBFX:
    {
        int lsb_val, width_val;
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        lsb_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        width_val = (int)as->tok.val; advance(as);
        emit_insn(as, a64_ubfm(rd, rn,
            lsb_val, lsb_val + width_val - 1));
        break;
    }

    /* SBFX Xd,Xn,#lsb,#w => SBFM Xd,Xn,#lsb,#(lsb+w-1) */
    case MN_SBFX:
    {
        int lsb_val, width_val;
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        lsb_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        width_val = (int)as->tok.val; advance(as);
        emit_insn(as, a64_sbfm(rd, rn,
            lsb_val, lsb_val + width_val - 1));
        break;
    }

    /* ---- BTI/PAC ---- */
    case MN_BTI:
    {
        int variant = 0;
        if (as->tok.kind == ASTOK_IDENT) {
            if (tok_eq(&as->tok, "j"))
                variant = 1;
            else if (tok_eq(&as->tok, "jc"))
                variant = 2;
            advance(as);
        }
        emit_insn(as, a64_bti(variant));
        break;
    }

    case MN_PACIASP:
        emit_insn(as, a64_paciasp());
        break;

    case MN_AUTIASP:
        emit_insn(as, a64_autiasp());
        break;

    /* ---- System instructions: DC ---- */
    case MN_DC:
    {
        /* dc <op>, Xt -- data cache operations
         * op: ivac(op1=0,CRn=7,CRm=6,op2=1),
         *     cvac(op1=3,CRn=7,CRm=10,op2=1),
         *     civac(op1=3,CRn=7,CRm=14,op2=1),
         *     zva(op1=3,CRn=7,CRm=4,op2=1) */
        int op1 = 0, crm = 0, op2 = 1;
        const char *op_name = as->tok.start;
        int op_len = as->tok.len;
        int rt_val = REG_XZR;
        advance(as); /* consume operation name */
        if (op_len == 4 && strncmp(op_name, "ivac", 4) == 0) {
            op1 = 0; crm = 6;
        } else if (op_len == 4 && strncmp(op_name, "cvac", 4) == 0) {
            op1 = 3; crm = 10;
        } else if (op_len == 5 && strncmp(op_name, "civac", 5) == 0) {
            op1 = 3; crm = 14;
        } else if (op_len == 3 && strncmp(op_name, "zva", 3) == 0) {
            op1 = 3; crm = 4;
        }
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            rt_val = (int)as->tok.val;
            advance(as);
        }
        emit_insn(as, a64_sys(op1, 7, crm, op2, rt_val));
        break;
    }

    /* ---- System instructions: IC ---- */
    case MN_IC:
    {
        /* ic iallu: op1=0,CRn=7,CRm=5,op2=0,Rt=xzr
         * ic ialluis: op1=0,CRn=7,CRm=1,op2=0,Rt=xzr
         * ic ivau, Xt: op1=3,CRn=7,CRm=5,op2=1 */
        int op1 = 0, crm = 5, op2v = 0;
        const char *op_name = as->tok.start;
        int op_len = as->tok.len;
        int rt_val = REG_XZR;
        advance(as);
        if (op_len == 5 && strncmp(op_name, "iallu", 5) == 0) {
            op1 = 0; crm = 5; op2v = 0;
        } else if (op_len == 7 && strncmp(op_name, "ialluis", 7) == 0) {
            op1 = 0; crm = 1; op2v = 0;
        } else if (op_len == 4 && strncmp(op_name, "ivau", 4) == 0) {
            op1 = 3; crm = 5; op2v = 1;
        }
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            rt_val = (int)as->tok.val;
            advance(as);
        }
        emit_insn(as, a64_sys(op1, 7, crm, op2v, rt_val));
        break;
    }

    /* ---- System instructions: TLBI ---- */
    case MN_TLBI:
    {
        /* tlbi vmalle1is: op1=0,CRn=8,CRm=3,op2=0,Rt=xzr
         * tlbi vale1is, Xt: op1=0,CRn=8,CRm=3,op2=5
         * tlbi aside1is, Xt: op1=0,CRn=8,CRm=3,op2=2
         * tlbi vaae1is, Xt: op1=0,CRn=8,CRm=3,op2=3
         * tlbi vmalle1: op1=0,CRn=8,CRm=7,op2=0
         * tlbi vale1, Xt: op1=0,CRn=8,CRm=7,op2=5
         * tlbi aside1, Xt: op1=0,CRn=8,CRm=7,op2=2 */
        int op1 = 0, crm = 3, op2v = 0;
        const char *op_name = as->tok.start;
        int op_len = as->tok.len;
        int rt_val = REG_XZR;
        advance(as);
        if (op_len == 9 && strncmp(op_name, "vmalle1is", 9) == 0) {
            crm = 3; op2v = 0;
        } else if (op_len == 7 && strncmp(op_name, "vale1is", 7) == 0) {
            crm = 3; op2v = 5;
        } else if (op_len == 8 && strncmp(op_name, "aside1is", 8) == 0) {
            crm = 3; op2v = 2;
        } else if (op_len == 7 && strncmp(op_name, "vaae1is", 7) == 0) {
            crm = 3; op2v = 3;
        } else if (op_len == 7 && strncmp(op_name, "vmalle1", 7) == 0) {
            crm = 7; op2v = 0;
        } else if (op_len == 5 && strncmp(op_name, "vale1", 5) == 0) {
            crm = 7; op2v = 5;
        } else if (op_len == 6 && strncmp(op_name, "aside1", 6) == 0) {
            crm = 7; op2v = 2;
        }
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            rt_val = (int)as->tok.val;
            advance(as);
        }
        emit_insn(as, a64_sys(op1, 8, crm, op2v, rt_val));
        break;
    }

    /* ---- System instructions: AT ---- */
    case MN_AT:
    {
        /* at s1e1r, Xt: op1=0,CRn=7,CRm=8,op2=0
         * at s1e0r, Xt: op1=0,CRn=7,CRm=8,op2=2
         * at s1e1w, Xt: op1=0,CRn=7,CRm=8,op2=1
         * at s1e0w, Xt: op1=0,CRn=7,CRm=8,op2=3 */
        int op2v = 0;
        const char *op_name = as->tok.start;
        int op_len = as->tok.len;
        int rt_val = REG_XZR;
        advance(as);
        if (op_len == 5 && strncmp(op_name, "s1e1r", 5) == 0) {
            op2v = 0;
        } else if (op_len == 5 && strncmp(op_name, "s1e0r", 5) == 0) {
            op2v = 2;
        } else if (op_len == 5 && strncmp(op_name, "s1e1w", 5) == 0) {
            op2v = 1;
        } else if (op_len == 5 && strncmp(op_name, "s1e0w", 5) == 0) {
            op2v = 3;
        }
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            rt_val = (int)as->tok.val;
            advance(as);
        }
        emit_insn(as, a64_sys(0, 7, 8, op2v, rt_val));
        break;
    }

    /* ---- HINT ---- */
    case MN_HINT:
        imm = 0;
        if (as->tok.kind == ASTOK_IMM) {
            imm = as->tok.val;
            advance(as);
        }
        emit_insn(as, a64_hint((int)imm));
        break;

    /* ---- CLREX ---- */
    case MN_CLREX:
        emit_insn(as, a64_clrex());
        break;

    /* ---- Load-acquire ---- */
    case MN_LDAR:
    {
        int is_w_ld;
        rd = (int)as->tok.val;
        is_w_ld = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        if (is_w_ld)
            emit_insn(as, a64_ldar_w(rd, rn));
        else
            emit_insn(as, a64_ldar(rd, rn));
        break;
    }

    case MN_LDARB:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_ldarb(rd, rn));
        break;

    case MN_LDARH:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_ldarh(rd, rn));
        break;

    /* ---- Store-release ---- */
    case MN_STLR:
    {
        int is_w_st;
        rd = (int)as->tok.val;
        is_w_st = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        if (is_w_st)
            emit_insn(as, a64_stlr_w(rd, rn));
        else
            emit_insn(as, a64_stlr(rd, rn));
        break;
    }

    case MN_STLRB:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_stlrb(rd, rn));
        break;

    case MN_STLRH:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_stlrh(rd, rn));
        break;

    /* ---- Conditional compare ---- */
    case MN_CCMP:
    case MN_CCMN:
    {
        int nzcv_val;
        rn = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_IMM) {
            /* immediate form: ccmp xn, #imm, #nzcv, cond */
            imm = as->tok.val; advance(as);
            expect(as, ASTOK_COMMA);
            nzcv_val = (int)as->tok.val; advance(as);
            expect(as, ASTOK_COMMA);
            cond = parse_cond(as->tok.start, as->tok.len);
            if (cond < 0) {
                fprintf(stderr, "as: line %d: invalid condition\n", line);
                exit(1);
            }
            advance(as);
            if (mn == MN_CCMP)
                emit_insn(as, a64_ccmp_i(rn, (int)imm, nzcv_val, cond));
            else
                emit_insn(as, a64_ccmn_i(rn, (int)imm, nzcv_val, cond));
        } else if (as->tok.kind == ASTOK_REG) {
            /* register form: ccmp xn, xm, #nzcv, cond */
            rm = (int)as->tok.val; advance(as);
            expect(as, ASTOK_COMMA);
            nzcv_val = (int)as->tok.val; advance(as);
            expect(as, ASTOK_COMMA);
            cond = parse_cond(as->tok.start, as->tok.len);
            if (cond < 0) {
                fprintf(stderr, "as: line %d: invalid condition\n", line);
                exit(1);
            }
            advance(as);
            emit_insn(as, a64_ccmp_r(rn, rm, nzcv_val, cond));
        }
        break;
    }

    /* ---- EXTR ---- */
    case MN_EXTR:
    {
        int lsb;
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        lsb = (int)as->tok.val; advance(as);
        emit_insn(as, a64_extr(rd, rn, rm, lsb));
        break;
    }

    /* ---- PRFM ---- */
    case MN_PRFM:
    {
        int prf_type = 0;
        /* parse prefetch type: could be identifier or immediate */
        if (as->tok.kind == ASTOK_IMM) {
            prf_type = (int)as->tok.val;
            advance(as);
        } else if (as->tok.kind == ASTOK_IDENT) {
            /* common names: pldl1keep=0, pldl1strm=1, etc. */
            if (tok_eq(&as->tok, "pldl1keep")) prf_type = 0;
            else if (tok_eq(&as->tok, "pldl1strm")) prf_type = 1;
            else if (tok_eq(&as->tok, "pldl2keep")) prf_type = 2;
            else if (tok_eq(&as->tok, "pldl2strm")) prf_type = 3;
            else if (tok_eq(&as->tok, "pldl3keep")) prf_type = 4;
            else if (tok_eq(&as->tok, "pldl3strm")) prf_type = 5;
            else if (tok_eq(&as->tok, "pstl1keep")) prf_type = 16;
            else if (tok_eq(&as->tok, "pstl1strm")) prf_type = 17;
            else if (tok_eq(&as->tok, "pstl2keep")) prf_type = 18;
            else if (tok_eq(&as->tok, "pstl2strm")) prf_type = 19;
            advance(as);
        }
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        imm = 0;
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_IMM) {
                imm = as->tok.val;
                advance(as);
            }
        }
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_prfm(prf_type, rn, (i32)imm));
        break;
    }

    /* ---- LDRSB, LDRSH (signed load to 64-bit) ---- */
    case MN_LDRSB:
    case MN_LDRSH:
    {
        int rt_val;
        rt_val = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val; advance(as);
        imm = 0;
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_IMM) {
                imm = as->tok.val;
                advance(as);
            }
        }
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        if (mn == MN_LDRSB)
            emit_insn(as, a64_ldrsb(rt_val, rn, (i32)imm));
        else
            emit_insn(as, a64_ldrsh(rt_val, rn, (i32)imm));
        break;
    }

    /* ---- MADD, SMADDL, UMADDL ---- */
    case MN_MADD:
    case MN_SMADDL:
    case MN_UMADDL:
        rd = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val; advance(as);
        expect(as, ASTOK_COMMA);
        ra = (int)as->tok.val; advance(as);
        if (mn == MN_MADD)
            emit_insn(as, a64_madd(rd, rn, rm, ra));
        else if (mn == MN_SMADDL)
            emit_insn(as, a64_smaddl(rd, rn, rm, ra));
        else
            emit_insn(as, a64_umaddl(rd, rn, rm, ra));
        break;


    /* ---- TST (test bits) ---- */
    case MN_TST:
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            imm = as->tok.val;
            advance(as);
            emit_insn(as, a64_tst_i(rn, (u64)imm));
        } else if (as->tok.kind == ASTOK_REG) {
            rm = (int)as->tok.val;
            advance(as);
            /* check for optional shift: , lsl #N */
            if (as->tok.kind == ASTOK_COMMA) {
                int shift_amt = 0;
                advance(as);
                if (as->tok.kind == ASTOK_IDENT &&
                    (tok_eq(&as->tok, "lsl") || tok_eq(&as->tok, "LSL"))) {
                    advance(as);
                    if (as->tok.kind == ASTOK_IMM) {
                        shift_amt = (int)as->tok.val;
                        advance(as);
                    }
                }
                /* ANDS XZR, Rn, Rm, LSL #shift
                 * 1_11_01010_00_0_Rm_imm6_Rn_Rd */
                emit_insn(as, (u32)0xEA000000
                    | (((u32)rm & 0x1F) << 16)
                    | (((u32)shift_amt & 0x3F) << 10)
                    | (((u32)rn & 0x1F) << 5)
                    | (u32)REG_XZR);
            } else {
                emit_insn(as, a64_tst_r(rn, rm));
            }
        }
        break;

    /* ---- ANDS ---- */
    case MN_ANDS:
        rd = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            imm = as->tok.val;
            advance(as);
            emit_insn(as, a64_ands_i(rd, rn, (u64)imm));
        } else {
            rm = (int)as->tok.val;
            advance(as);
            emit_insn(as, a64_ands_r(rd, rn, rm));
        }
        break;

    /* ---- BICS ---- */
    case MN_BICS:
        rd = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val;
        advance(as);
        emit_insn(as, a64_bics_r(rd, rn, rm));
        break;

    /* ---- ORN ---- */
    case MN_ORN:
    {
        int orn_shift_type = 0, orn_shift_amt = 0;
        rd = (int)as->tok.val;
        is_wreg_d = as->tok.is_wreg;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rm = (int)as->tok.val;
        advance(as);
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_IDENT) {
                if (tok_eq(&as->tok, "lsl") || tok_eq(&as->tok, "LSL"))
                    orn_shift_type = 0;
                else if (tok_eq(&as->tok, "lsr") || tok_eq(&as->tok, "LSR"))
                    orn_shift_type = 1;
                else if (tok_eq(&as->tok, "asr") || tok_eq(&as->tok, "ASR"))
                    orn_shift_type = 2;
                advance(as);
                if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
                    orn_shift_amt = (int)as->tok.val;
                    advance(as);
                }
            }
        }
        if (orn_shift_amt == 0 && orn_shift_type == 0) {
            emit_insn(as, a64_orn_r(rd, rn, rm));
        } else {
            /* ORN shifted: sf_01_01010_sh_1_Rm_imm6_Rn_Rd */
            u32 orn_base = is_wreg_d ? (u32)0x2A200000 : (u32)0xAA200000;
            emit_insn(as, orn_base
                | (((u32)orn_shift_type & 3) << 22)
                | (((u32)rm & 0x1F) << 16)
                | (((u32)orn_shift_amt & 0x3F) << 10)
                | (((u32)rn & 0x1F) << 5)
                | ((u32)rd & 0x1F));
        }
        break;
    }

    /* ---- STNP (store non-temporal pair) ---- */
    case MN_STNP:
    {
        int rt1, rt2;
        rt1 = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rt2 = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val;
        advance(as);
        imm = 0;
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_IMM) {
                imm = as->tok.val;
                advance(as);
            }
        }
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_stnp(rt1, rt2, rn, (i32)imm));
        break;
    }

    /* ---- LDNP (load non-temporal pair) ---- */
    case MN_LDNP:
    {
        int rt1, rt2;
        rt1 = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rt2 = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        expect(as, ASTOK_LBRACKET);
        rn = (int)as->tok.val;
        advance(as);
        imm = 0;
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_IMM) {
                imm = as->tok.val;
                advance(as);
            }
        }
        if (as->tok.kind == ASTOK_RBRACKET) advance(as);
        emit_insn(as, a64_ldnp(rt1, rt2, rn, (i32)imm));
        break;
    }

    /* ---- ADDS ---- */
    case MN_ADDS:
        rd = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        rn = (int)as->tok.val;
        advance(as);
        expect(as, ASTOK_COMMA);
        if (as->tok.kind == ASTOK_REG) {
            rm = (int)as->tok.val;
            advance(as);
            emit_insn(as, a64_adds_r(rd, rn, rm));
        } else if (as->tok.kind == ASTOK_IMM) {
            imm = as->tok.val;
            advance(as);
            emit_insn(as, a64_adds_i(rd, rn, (u32)imm));
        }
        break;

    /* ---- SYS ---- */
    case MN_SYS:
    {
        int op1v, crn_v, crm_v, op2v, rt_v;
        op1v = 0; crn_v = 0; crm_v = 0; op2v = 0; rt_v = REG_XZR;
        if (as->tok.kind == ASTOK_IMM || as->tok.kind == ASTOK_NUM) {
            op1v = (int)as->tok.val; advance(as);
        }
        if (as->tok.kind == ASTOK_COMMA) advance(as);
        /* CRn: expect cN or CN or just a number */
        if (as->tok.kind == ASTOK_IDENT || as->tok.kind == ASTOK_REG) {
            /* skip cN-style register name */
            advance(as);
        } else if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
            crn_v = (int)as->tok.val; advance(as);
        }
        if (as->tok.kind == ASTOK_COMMA) advance(as);
        if (as->tok.kind == ASTOK_IDENT || as->tok.kind == ASTOK_REG) {
            advance(as);
        } else if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
            crm_v = (int)as->tok.val; advance(as);
        }
        if (as->tok.kind == ASTOK_COMMA) advance(as);
        if (as->tok.kind == ASTOK_NUM || as->tok.kind == ASTOK_IMM) {
            op2v = (int)as->tok.val; advance(as);
        }
        if (as->tok.kind == ASTOK_COMMA) {
            advance(as);
            if (as->tok.kind == ASTOK_REG) {
                rt_v = (int)as->tok.val; advance(as);
            }
        }
        emit_insn(as, a64_sys(op1v, crn_v, crm_v, op2v, rt_v));
        break;
    }

    default:
        /* unknown instruction -- skip to end of line */
        while (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF) {
            advance(as);
        }
        break;
    }

    (void)is_wreg_n;
    (void)ra;
}

/* ---- Main assembly pass ---- */

static void assemble_pass(struct assembler *as, const char *src)
{
    int sym_idx;
    int macro_idx;

    as_lex_init(&as->lex, src);
    advance(as);

    while (as->tok.kind != ASTOK_EOF) {
        skip_newlines(as);
        if (as->tok.kind == ASTOK_EOF) break;

        /* ---- Macro body recording ---- */
        if (as->in_macro_def) {
            if (as->tok.kind == ASTOK_DIRECTIVE && tok_eq(&as->tok, ".endm")) {
                as->in_macro_def = 0;
                advance(as);
                continue;
            }
            /* Record raw source text of this line into macro body */
            {
                struct asm_macro *m = &as->macros[as->cur_macro];
                const char *line_start = as->tok.start;
                const char *line_end;
                int copy_len;
                /* skip tokens to find line end */
                while (as->tok.kind != ASTOK_NEWLINE &&
                       as->tok.kind != ASTOK_EOF)
                    advance(as);
                /* line_end = start of newline token or current pos */
                line_end = (as->tok.kind == ASTOK_NEWLINE) ?
                    as->tok.start : as->lex.pos;
                copy_len = (int)(line_end - line_start);
                if (copy_len > 0 &&
                    m->body_len + copy_len + 2 < MAX_MACRO_BODY) {
                    memcpy(m->body + m->body_len, line_start,
                           (size_t)copy_len);
                    m->body_len += copy_len;
                }
                if (m->body_len + 1 < MAX_MACRO_BODY)
                    m->body[m->body_len++] = '\n';
                m->body[m->body_len] = '\0';
            }
            continue;
        }

        /* ---- Repeat body recording ---- */
        if (as->in_rept) {
            if (as->tok.kind == ASTOK_DIRECTIVE && tok_eq(&as->tok, ".endr")) {
                int ri;
                as->in_rept = 0;
                as->rept_body[as->rept_body_len] = '\0';
                advance(as);
                /* expand the repeat block rept_count times */
                for (ri = 0; ri < as->rept_count; ri++) {
                    struct as_lexer saved_lex = as->lex;
                    struct as_token saved_tok = as->tok;
                    assemble_pass(as, as->rept_body);
                    as->lex = saved_lex;
                    as->tok = saved_tok;
                }
                continue;
            }
            /* Record raw source text of this line */
            {
                const char *ls = as->tok.start;
                const char *le;
                int cl;
                while (as->tok.kind != ASTOK_NEWLINE &&
                       as->tok.kind != ASTOK_EOF)
                    advance(as);
                le = (as->tok.kind == ASTOK_NEWLINE) ?
                    as->tok.start : as->lex.pos;
                cl = (int)(le - ls);
                if (cl > 0 &&
                    as->rept_body_len + cl + 2 < MAX_REPT_BODY) {
                    memcpy(as->rept_body + as->rept_body_len,
                           ls, (size_t)cl);
                    as->rept_body_len += cl;
                }
                if (as->rept_body_len + 1 < MAX_REPT_BODY)
                    as->rept_body[as->rept_body_len++] = '\n';
            }
            continue;
        }

        /* ---- IRP/IRPC body recording ---- */
        if (as->in_irp) {
            if (as->tok.kind == ASTOK_DIRECTIVE && tok_eq(&as->tok, ".endr")) {
                int irp_mode = as->in_irp; /* 1=irp, 2=irpc */
                int irp_count;
                int ii;
                as->in_irp = 0;
                as->irp_body[as->irp_body_len] = '\0';
                advance(as);

                irp_count = (irp_mode == 1) ?
                    as->irp_num_values : as->irp_num_chars;

                for (ii = 0; ii < irp_count; ii++) {
                    /* Build expanded body: substitute \var with value */
                    char expanded[MAX_IRP_BODY * 2];
                    const char *val_str;
                    char char_buf[2];
                    int val_len;
                    int var_len = (int)strlen(as->irp_var);
                    int bi, ei = 0;

                    if (irp_mode == 1) {
                        val_str = as->irp_values[ii];
                        val_len = (int)strlen(val_str);
                    } else {
                        char_buf[0] = as->irp_chars[ii];
                        char_buf[1] = '\0';
                        val_str = char_buf;
                        val_len = 1;
                    }

                    for (bi = 0; bi < as->irp_body_len &&
                         ei < (int)sizeof(expanded) - 128; bi++) {
                        if (as->irp_body[bi] == '\\' &&
                            bi + 1 + var_len <= as->irp_body_len &&
                            strncmp(as->irp_body + bi + 1,
                                    as->irp_var, (size_t)var_len) == 0) {
                            memcpy(expanded + ei, val_str, (size_t)val_len);
                            ei += val_len;
                            bi += var_len; /* skip var name */
                        } else {
                            expanded[ei++] = as->irp_body[bi];
                        }
                    }
                    expanded[ei] = '\0';

                    {
                        struct as_lexer saved_lex = as->lex;
                        struct as_token saved_tok = as->tok;
                        assemble_pass(as, expanded);
                        as->lex = saved_lex;
                        as->tok = saved_tok;
                    }
                }
                continue;
            }
            /* Record raw source text of this line into irp body */
            {
                const char *ls = as->tok.start;
                const char *le;
                int cl;
                while (as->tok.kind != ASTOK_NEWLINE &&
                       as->tok.kind != ASTOK_EOF)
                    advance(as);
                le = (as->tok.kind == ASTOK_NEWLINE) ?
                    as->tok.start : as->lex.pos;
                cl = (int)(le - ls);
                if (cl > 0 &&
                    as->irp_body_len + cl + 2 < MAX_IRP_BODY) {
                    memcpy(as->irp_body + as->irp_body_len,
                           ls, (size_t)cl);
                    as->irp_body_len += cl;
                }
                if (as->irp_body_len + 1 < MAX_IRP_BODY)
                    as->irp_body[as->irp_body_len++] = '\n';
            }
            continue;
        }

        /* ---- Conditional assembly: must handle .if/.else/.endif
         *      even when skipping, to track nesting ---- */
        if (as->tok.kind == ASTOK_DIRECTIVE) {
            if (tok_eq(&as->tok, ".if") || tok_eq(&as->tok, ".ifdef") ||
                tok_eq(&as->tok, ".ifndef") || tok_eq(&as->tok, ".ifb") ||
                tok_eq(&as->tok, ".ifnb") || tok_eq(&as->tok, ".else") ||
                tok_eq(&as->tok, ".endif")) {
                parse_directive(as);
                continue;
            }
        }

        /* If conditional assembly says skip, skip this line */
        if (!is_cond_active(as)) {
            while (as->tok.kind != ASTOK_NEWLINE && as->tok.kind != ASTOK_EOF)
                advance(as);
            continue;
        }

        /* ---- Numeric label (e.g. 1:, 661:) ---- */
        if (as->tok.kind == ASTOK_LABEL &&
            as->tok.len >= 1 &&
            as->tok.start[0] >= '0' && as->tok.start[0] <= '9') {
            int digit = (int)as->tok.val;
            char synname[32];
            add_local_label(as, digit);
            /* create a synthetic symbol for this instance */
            sprintf(synname, ".LL%d_%d",
                    digit, as->local_label_seq - 1);
            sym_idx = add_sym(as, synname, (int)strlen(synname));
            as->syms[sym_idx].section = as->cur_section;
            as->syms[sym_idx].value = cur_offset(as);
            as->syms[sym_idx].is_defined = 1;
            advance(as);
            continue;
        }

        /* label */
        if (as->tok.kind == ASTOK_LABEL) {
            sym_idx = add_sym(as, as->tok.start, as->tok.len);
            as->syms[sym_idx].section = as->cur_section;
            as->syms[sym_idx].value = cur_offset(as);
            as->syms[sym_idx].is_defined = 1;
            advance(as);
            continue;
        }

        /* directive */
        if (as->tok.kind == ASTOK_DIRECTIVE) {
            parse_directive(as);
            continue;
        }

        /* instruction or macro invocation */
        if (as->tok.kind == ASTOK_IDENT) {
            /* check for register alias: name .req xN */
            {
                const char *alias_name = as->tok.start;
                int alias_len = as->tok.len;
                /* peek at next token to see if it's .req */
                const char *saved_pos = as->lex.pos;
                int saved_line = as->lex.line;
                struct as_token peek_tok;
                as_next_token(&as->lex, &peek_tok);
                as->lex.pos = saved_pos;
                as->lex.line = saved_line;
                if (peek_tok.kind == ASTOK_DIRECTIVE &&
                    peek_tok.len == 4 &&
                    strncmp(peek_tok.start, ".req", 4) == 0) {
                    /* name .req register */
                    advance(as); /* consume alias name */
                    advance(as); /* consume .req */
                    if (as->tok.kind == ASTOK_REG) {
                        if (as->num_reg_aliases < MAX_REG_ALIASES) {
                            struct reg_alias *ra;
                            ra = &as->reg_aliases[as->num_reg_aliases];
                            if (alias_len > 63) alias_len = 63;
                            memcpy(ra->name, alias_name, (size_t)alias_len);
                            ra->name[alias_len] = '\0';
                            ra->reg = (int)as->tok.val;
                            ra->is_wreg = as->tok.is_wreg;
                            as->num_reg_aliases++;
                        }
                        advance(as);
                    } else if (as->tok.kind == ASTOK_IDENT) {
                        /* might be aliasing another alias */
                        int ai = find_reg_alias(as,
                                                as->tok.start, as->tok.len);
                        if (ai >= 0 &&
                            as->num_reg_aliases < MAX_REG_ALIASES) {
                            struct reg_alias *ra;
                            ra = &as->reg_aliases[as->num_reg_aliases];
                            if (alias_len > 63) alias_len = 63;
                            memcpy(ra->name, alias_name, (size_t)alias_len);
                            ra->name[alias_len] = '\0';
                            ra->reg = as->reg_aliases[ai].reg;
                            ra->is_wreg = as->reg_aliases[ai].is_wreg;
                            as->num_reg_aliases++;
                        }
                        advance(as);
                    }
                    continue;
                }
            }
            /* check for numeric label ref used as a bare ident at
             * statement level -- not applicable; those come inside insns */
            /* check for macro invocation */
            macro_idx = find_macro(as, as->tok.start, as->tok.len);
            if (macro_idx >= 0) {
                advance(as); /* consume macro name */
                expand_macro(as, macro_idx);
                continue;
            }
            parse_instruction(as);
            continue;
        }

        /* skip anything else */
        advance(as);
    }
}

/* ---- ELF writing ---- */

static void write_bytes(FILE *f, const void *data, u64 size)
{
    if (size > 0) {
        fwrite(data, 1, (size_t)size, f);
    }
}

static void write_padding(FILE *f, u64 count)
{
    u64 i;
    u8 zero = 0;

    for (i = 0; i < count; i++) {
        fwrite(&zero, 1, 1, f);
    }
}

static u64 align_up(u64 val, u64 alignment)
{
    return (val + alignment - 1) & ~(alignment - 1);
}

void emit_elf(struct assembler *as, const char *outpath)
{
    FILE *f;
    Elf64_Ehdr ehdr;
    Elf64_Shdr *shdrs;
    Elf64_Sym *symtab;
    Elf64_Rela **rela_tabs;
    int *rela_counts;
    u64 offset;
    int i;
    int num_locals, num_globals;
    int sym_count;
    u32 name_off;
    int content_count;
    int num_content_secs;
    int num_rela_secs;
    int total_shdrs;
    int idx_symtab, idx_strtab, idx_shstrtab;
    int *rela_shdr_idx;
    u32 *sh_names;
    u64 *sh_offsets;

    content_count = as->num_sections;

    /* ---- Count relocations per content section ---- */
    rela_counts = (int *)calloc((size_t)content_count, sizeof(int));
    if (!rela_counts) { fprintf(stderr, "as: out of memory\n"); exit(1); }

    for (i = 0; i < as->num_relocs; i++) {
        int s = as->relocs[i].section;
        if (s >= 0 && s < content_count)
            rela_counts[s]++;
    }

    num_rela_secs = 0;
    for (i = 1; i < content_count; i++) {
        if (rela_counts[i] > 0) num_rela_secs++;
    }

    /* ---- Compute total ELF section count ---- */
    total_shdrs = content_count + 3 + num_rela_secs;
    idx_symtab = content_count;
    idx_strtab = content_count + 1;
    idx_shstrtab = content_count + 2;

    /* ---- Build section header string table ---- */
    as->shstrtab_size = 0;
    add_shstrtab(as, "");

    sh_names = (u32 *)calloc((size_t)total_shdrs, sizeof(u32));
    if (!sh_names) { fprintf(stderr, "as: out of memory\n"); exit(1); }

    for (i = 1; i < content_count; i++) {
        sh_names[i] = add_shstrtab(as, as->sections[i].name);
    }
    sh_names[idx_symtab] = add_shstrtab(as, ".symtab");
    sh_names[idx_strtab] = add_shstrtab(as, ".strtab");
    sh_names[idx_shstrtab] = add_shstrtab(as, ".shstrtab");

    rela_shdr_idx = (int *)calloc((size_t)content_count, sizeof(int));
    if (!rela_shdr_idx) { fprintf(stderr, "as: out of memory\n"); exit(1); }
    {
        int rela_idx = content_count + 3;
        for (i = 1; i < content_count; i++) {
            if (rela_counts[i] > 0) {
                char rela_name[140];
                sprintf(rela_name, ".rela%s", as->sections[i].name);
                sh_names[rela_idx] = add_shstrtab(as, rela_name);
                rela_shdr_idx[i] = rela_idx;
                rela_idx++;
            }
        }
    }

    /* ---- Build string table and assign symbol indices ---- */
    as->strtab_size = 0;
    add_strtab(as, "", 0);

    /* Ensure all undefined symbols are marked global */
    for (i = 0; i < as->num_syms; i++) {
        if (!as->syms[i].is_defined && !as->syms[i].is_global) {
            as->syms[i].is_global = 1;
            as->syms[i].binding = STB_GLOBAL;
        }
    }

    /* Count symbols */
    num_locals = 0;
    num_globals = 0;
    num_content_secs = content_count - 1;
    for (i = 0; i < as->num_syms; i++) {
        if (as->syms[i].is_global)
            num_globals++;
        else if (as->syms[i].is_defined)
            num_locals++;
    }

    sym_count = 1 + num_content_secs + num_locals + num_globals;
    symtab = (Elf64_Sym *)calloc((size_t)sym_count, sizeof(Elf64_Sym));
    if (!symtab) { fprintf(stderr, "as: out of memory\n"); exit(1); }

    memset(&symtab[0], 0, sizeof(Elf64_Sym));

    /* section symbols for all content sections */
    for (i = 0; i < num_content_secs; i++) {
        symtab[1 + i].st_name = 0;
        symtab[1 + i].st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
        symtab[1 + i].st_other = 0;
        symtab[1 + i].st_shndx = (u16)(i + 1);
        symtab[1 + i].st_value = 0;
        symtab[1 + i].st_size = 0;
    }

    /* local defined symbols */
    {
        int out_idx = 1 + num_content_secs;

        for (i = 0; i < as->num_syms; i++) {
            if (!as->syms[i].is_global && as->syms[i].is_defined) {
                name_off = add_strtab(as, as->syms[i].name,
                                      (int)strlen(as->syms[i].name));
                symtab[out_idx].st_name = name_off;
                symtab[out_idx].st_info = ELF64_ST_INFO(
                    (u8)as->syms[i].binding,
                    (u8)as->syms[i].sym_type);
                symtab[out_idx].st_other = (u8)as->syms[i].visibility;
                symtab[out_idx].st_shndx = (u16)as->syms[i].section;
                symtab[out_idx].st_value = as->syms[i].value;
                symtab[out_idx].st_size = as->syms[i].sym_size;
                as->syms[i].index = out_idx;
                out_idx++;
            }
        }

        num_locals = out_idx;

        /* global symbols (includes weak) */
        for (i = 0; i < as->num_syms; i++) {
            if (as->syms[i].is_global) {
                name_off = add_strtab(as, as->syms[i].name,
                                      (int)strlen(as->syms[i].name));
                symtab[out_idx].st_name = name_off;
                symtab[out_idx].st_info = ELF64_ST_INFO(
                    (u8)as->syms[i].binding,
                    (u8)as->syms[i].sym_type);
                symtab[out_idx].st_other = (u8)as->syms[i].visibility;
                if (as->syms[i].is_defined) {
                    symtab[out_idx].st_shndx = (u16)as->syms[i].section;
                    symtab[out_idx].st_value = as->syms[i].value;
                } else {
                    symtab[out_idx].st_shndx = SHN_UNDEF;
                    symtab[out_idx].st_value = 0;
                }
                symtab[out_idx].st_size = as->syms[i].sym_size;
                as->syms[i].index = out_idx;
                out_idx++;
            }
        }

        sym_count = out_idx;
    }

    /* ---- Build per-section relocation tables ---- */
    rela_tabs = (Elf64_Rela **)calloc((size_t)content_count,
                                      sizeof(Elf64_Rela *));
    if (!rela_tabs) { fprintf(stderr, "as: out of memory\n"); exit(1); }

    for (i = 1; i < content_count; i++) {
        if (rela_counts[i] > 0) {
            rela_tabs[i] = (Elf64_Rela *)calloc((size_t)rela_counts[i],
                                                 sizeof(Elf64_Rela));
            if (!rela_tabs[i]) {
                fprintf(stderr, "as: out of memory\n"); exit(1);
            }
        }
    }

    {
        int *rela_fill = (int *)calloc((size_t)content_count, sizeof(int));
        if (!rela_fill) { fprintf(stderr, "as: out of memory\n"); exit(1); }

        for (i = 0; i < as->num_relocs; i++) {
            int s = as->relocs[i].section;
            int si = as->relocs[i].sym_index;
            int ri;
            if (s < 1 || s >= content_count) continue;
            ri = rela_fill[s]++;
            rela_tabs[s][ri].r_offset = as->relocs[i].offset;
            rela_tabs[s][ri].r_info = ELF64_R_INFO(
                (u64)as->syms[si].index,
                (u32)as->relocs[i].rtype);
            rela_tabs[s][ri].r_addend = as->relocs[i].addend;
        }
        free(rela_fill);
    }

    /* ---- Allocate and compute section headers ---- */
    shdrs = (Elf64_Shdr *)calloc((size_t)total_shdrs, sizeof(Elf64_Shdr));
    if (!shdrs) { fprintf(stderr, "as: out of memory\n"); exit(1); }

    sh_offsets = (u64 *)calloc((size_t)total_shdrs, sizeof(u64));
    if (!sh_offsets) { fprintf(stderr, "as: out of memory\n"); exit(1); }

    offset = sizeof(Elf64_Ehdr);

    /* Content sections */
    for (i = 1; i < content_count; i++) {
        struct asm_section *sec = &as->sections[i];
        u32 al = sec->alignment > 0 ? sec->alignment : 1;

        offset = align_up(offset, al);
        shdrs[i].sh_name = sh_names[i];
        shdrs[i].sh_type = sec->sh_type;
        shdrs[i].sh_flags = sec->sh_flags;
        shdrs[i].sh_offset = offset;
        shdrs[i].sh_size = sec->size;
        shdrs[i].sh_addralign = al;
        shdrs[i].sh_entsize = sec->entsize;
        sh_offsets[i] = offset;

        if (sec->sh_type != SHT_NOBITS)
            offset += sec->size;
    }

    /* .symtab */
    offset = align_up(offset, 8);
    shdrs[idx_symtab].sh_name = sh_names[idx_symtab];
    shdrs[idx_symtab].sh_type = SHT_SYMTAB;
    shdrs[idx_symtab].sh_offset = offset;
    shdrs[idx_symtab].sh_size = (u64)sym_count * sizeof(Elf64_Sym);
    shdrs[idx_symtab].sh_link = (u32)idx_strtab;
    shdrs[idx_symtab].sh_info = (u32)num_locals;
    shdrs[idx_symtab].sh_addralign = 8;
    shdrs[idx_symtab].sh_entsize = sizeof(Elf64_Sym);
    sh_offsets[idx_symtab] = offset;
    offset += shdrs[idx_symtab].sh_size;

    /* .strtab */
    shdrs[idx_strtab].sh_name = sh_names[idx_strtab];
    shdrs[idx_strtab].sh_type = SHT_STRTAB;
    shdrs[idx_strtab].sh_offset = offset;
    shdrs[idx_strtab].sh_size = as->strtab_size;
    shdrs[idx_strtab].sh_addralign = 1;
    sh_offsets[idx_strtab] = offset;
    offset += as->strtab_size;

    /* .shstrtab */
    shdrs[idx_shstrtab].sh_name = sh_names[idx_shstrtab];
    shdrs[idx_shstrtab].sh_type = SHT_STRTAB;
    shdrs[idx_shstrtab].sh_offset = offset;
    shdrs[idx_shstrtab].sh_size = as->shstrtab_size;
    shdrs[idx_shstrtab].sh_addralign = 1;
    sh_offsets[idx_shstrtab] = offset;
    offset += as->shstrtab_size;

    /* .rela.* sections */
    {
        int rela_idx = content_count + 3;
        for (i = 1; i < content_count; i++) {
            if (rela_counts[i] > 0) {
                offset = align_up(offset, 8);
                shdrs[rela_idx].sh_name = sh_names[rela_idx];
                shdrs[rela_idx].sh_type = SHT_RELA;
                shdrs[rela_idx].sh_flags = SHF_INFO_LINK;
                shdrs[rela_idx].sh_offset = offset;
                shdrs[rela_idx].sh_size = (u64)rela_counts[i]
                                           * sizeof(Elf64_Rela);
                shdrs[rela_idx].sh_link = (u32)idx_symtab;
                shdrs[rela_idx].sh_info = (u32)i;
                shdrs[rela_idx].sh_addralign = 8;
                shdrs[rela_idx].sh_entsize = sizeof(Elf64_Rela);
                sh_offsets[rela_idx] = offset;
                offset += shdrs[rela_idx].sh_size;
                rela_idx++;
            }
        }
    }

    /* section header table follows */
    offset = align_up(offset, 8);

    /* ---- Build ELF header ---- */
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_ident[7] = ELFOSABI_NONE;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_AARCH64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = 0;
    ehdr.e_phoff = 0;
    ehdr.e_shoff = offset;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = 0;
    ehdr.e_phnum = 0;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = (u16)total_shdrs;
    ehdr.e_shstrndx = (u16)idx_shstrtab;

    /* ---- Write file ---- */
    f = fopen(outpath, "wb");
    if (!f) {
        fprintf(stderr, "as: cannot open output file: %s\n", outpath);
        exit(1);
    }

    /* ELF header */
    write_bytes(f, &ehdr, sizeof(ehdr));

    /* Content sections and meta sections */
    {
        u64 cur_pos = sizeof(Elf64_Ehdr);

        for (i = 1; i < content_count; i++) {
            struct asm_section *sec = &as->sections[i];
            if (sec->sh_type == SHT_NOBITS) continue;
            if (sec->size == 0) continue;
            write_padding(f, sh_offsets[i] - cur_pos);
            write_bytes(f, sec->data, sec->size);
            cur_pos = sh_offsets[i] + sec->size;
        }

        /* .symtab */
        write_padding(f, sh_offsets[idx_symtab] - cur_pos);
        write_bytes(f, symtab, (u64)sym_count * sizeof(Elf64_Sym));
        cur_pos = sh_offsets[idx_symtab]
                + (u64)sym_count * sizeof(Elf64_Sym);

        /* .strtab */
        write_bytes(f, as->strtab, as->strtab_size);
        cur_pos += as->strtab_size;

        /* .shstrtab */
        write_bytes(f, as->shstrtab, as->shstrtab_size);
        cur_pos += as->shstrtab_size;

        /* .rela.* sections */
        for (i = 1; i < content_count; i++) {
            if (rela_counts[i] > 0 && rela_tabs[i]) {
                int ridx = rela_shdr_idx[i];
                write_padding(f, sh_offsets[ridx] - cur_pos);
                write_bytes(f, rela_tabs[i],
                            (u64)rela_counts[i] * sizeof(Elf64_Rela));
                cur_pos = sh_offsets[ridx]
                        + (u64)rela_counts[i] * sizeof(Elf64_Rela);
            }
        }

        /* section header table */
        write_padding(f, ehdr.e_shoff - cur_pos);
    }
    write_bytes(f, shdrs, (u64)total_shdrs * sizeof(Elf64_Shdr));

    fclose(f);

    /* ---- Cleanup ---- */
    free(symtab);
    for (i = 0; i < content_count; i++) {
        if (rela_tabs[i]) free(rela_tabs[i]);
    }
    free(rela_tabs);
    free(rela_counts);
    free(rela_shdr_idx);
    free(sh_names);
    free(sh_offsets);
    free(shdrs);
}

/* ---- Two-pass assembly ---- */

void assemble(const char *src, const char *outpath)
{
    struct assembler as;
    int i;

    memset(&as, 0, sizeof(as));
    init_sections(&as);
    as.cur_section = SEC_TEXT;

    /* Pass 1: collect labels and sizes */
    as.pass = 1;
    assemble_pass(&as, src);

    /* Reset section sizes for pass 2 (but keep symbols and macros) */
    for (i = 0; i < as.num_sections; i++) {
        as.sections[i].size = 0;
    }
    as.num_relocs = 0;
    as.cur_section = SEC_TEXT;
    as.cond_depth = 0;
    as.sec_stack_top = 0;
    as.prev_section = SEC_TEXT;
    as.in_macro_def = 0;
    as.in_rept = 0;
    as.num_local_labels = 0;
    as.local_label_seq = 0;

    /* Pass 2: encode instructions and emit data */
    as.pass = 2;
    assemble_pass(&as, src);

    /* Write ELF object */
    emit_elf(&as, outpath);

    /* Free section data buffers */
    for (i = 0; i < as.num_sections; i++) {
        if (as.sections[i].data)
            free(as.sections[i].data);
    }
}
