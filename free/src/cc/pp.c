/*
 * pp.c - C89 preprocessor for the free C compiler.
 * Pure C89. All variables at top of block.
 *
 * Operates at the token level, using lex.c for tokenization.
 * Handles: #include, #define (object + function-like), #undef,
 *          #ifdef/#ifndef/#if/#elif/#else/#endif,
 *          # (stringify) and ## (paste) in macro bodies,
 *          #pragma once, __FILE__, __LINE__.
 */

#include "free.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ---- extern: builtin/attribute lookup (ext_builtins.c, ext_attrs.c) ---- */
extern int builtin_is_known(const char *name);

/* ---- lexer interface (from lex.c) ---- */
void lex_init(const char *src, const char *filename, struct arena *a);
struct tok *lex_next(void);
struct tok *lex_peek(void);
const char *lex_get_pos(void);
void lex_set_pos(const char *pos, int line, int col);
int lex_get_line(void);
int lex_get_col(void);
const char *lex_get_filename(void);
void lex_set_filename(const char *fn);
enum tok_kind lex_keyword_kind(const char *name, int len);

/* ---- limits ---- */
#define PP_MAX_MACROS      32768
#define PP_MAX_PARAMS      64
#define PP_MAX_MACRO_BODY  1024  /* max tokens in single macro defn line */
/* Kernel macro chains can expand far beyond a few thousand tokens. */
#define PP_MAX_BODY        65536  /* max tokens in expanded macro body */
#define PP_MAX_INCLUDE     64
#define PP_MAX_COND        128
#define PP_MAX_PATHS       64
#define PP_MAX_PRAGMA      512
#define PP_MAX_EXPAND      65536
#define PP_BUF_SIZE        (256 * 1024)

/* ---- macro definition ---- */
struct pp_macro {
    char *name;
    int is_func;            /* function-like macro */
    int nparams;
    char *params[PP_MAX_PARAMS];
    int is_variadic;
    char *va_name;          /* GNU named variadic: "args" in (fmt, args...) */
    struct tok *body;       /* arena-allocated body tokens */
    int body_len;
    int body_cap;           /* allocated capacity */
    int disabled;           /* to prevent recursive expansion */
};

/* ---- include stack ---- */
struct pp_include_frame {
    const char *src;        /* source text */
    const char *pos;        /* saved position */
    const char *filename;
    int line;
    int col;
    int include_path_idx;   /* which -I path this file came from, -1=none */
};

/* ---- conditional stack ---- */
struct pp_cond {
    int active;             /* are we emitting tokens */
    int has_been_true;      /* has any branch been true */
    int is_else;            /* have we seen #else */
};

/* ---- compiler flags accessed from cc.c ---- */
extern int cc_target_arch;
extern int cc_freestanding;
extern int cc_function_sections;
extern int cc_data_sections;
extern int cc_general_regs_only;
extern int cc_nostdinc;

/* target architecture constants (must match cc.c) */
#define TARGET_AARCH64  0
#define TARGET_X86_64   1

/* ---- hash table for macro lookup ---- */
#define PP_HASH_SIZE 4096  /* must be power of 2 */
#define PP_HASH_MASK (PP_HASH_SIZE - 1)

static unsigned int pp_hash_str(const char *s)
{
    unsigned int h;

    h = 5381;
    while (*s) {
        h = ((h << 5) + h) ^ (unsigned char)*s;
        s++;
    }
    return h;
}

/* Return non-zero for tokens that carry an identifier-like name.
 * Keywords are tokenized separately, but they still need to behave
 * like names in the preprocessor when used as macro identifiers or
 * macro parameters. */
static int pp_tok_is_name(struct tok *t)
{
    if (t->str == NULL || t->len == 0) {
        return 0;
    }
    if (t->kind == TOK_IDENT) {
        return 1;
    }
    if (t->kind >= TOK_AUTO && t->kind <= TOK_COMPLEX) {
        return 1;
    }
    return 0;
}

/* Hash table: each bucket is an index into pp_macros[], -1 = empty.
 * pp_macro_next[] forms a chain for collisions. */
static int pp_hash_buckets[PP_HASH_SIZE];
static int pp_macro_next[PP_MAX_MACROS]; /* next in hash chain, -1 = end */

/* ---- include guard cache ---- */
#define PP_GUARD_HASH_SIZE 2048
#define PP_GUARD_HASH_MASK (PP_GUARD_HASH_SIZE - 1)

struct pp_guard_entry {
    char *filename;    /* full path of the file */
    char *guard_macro; /* the include guard macro name */
};
static struct pp_guard_entry pp_guard_cache[PP_GUARD_HASH_SIZE];
static int pp_nguards;

/* ---- pragma once hash table ---- */
#define PP_ONCE_HASH_SIZE 1024
#define PP_ONCE_HASH_MASK (PP_ONCE_HASH_SIZE - 1)

struct pp_once_entry {
    char *filename;
    struct pp_once_entry *next;
};
static struct pp_once_entry *pp_once_buckets[PP_ONCE_HASH_SIZE];
static struct pp_once_entry pp_once_pool[PP_MAX_PRAGMA];
static int pp_nonce_pool;

/* ---- static state ---- */
static struct arena *pp_arena;
static struct pp_macro pp_macros[PP_MAX_MACROS];
static int pp_nmacros;

static struct pp_include_frame pp_inc_stack[PP_MAX_INCLUDE];
static int pp_inc_depth;

static struct pp_cond pp_cond_stack[PP_MAX_COND];
static int pp_cond_depth;

static char *pp_include_paths[PP_MAX_PATHS];
static int pp_ninclude_paths;

/* push_macro / pop_macro stack */
#define PP_MAX_MACRO_STACK 64
struct pp_macro_save {
    char name[128];       /* macro name */
    struct pp_macro macro; /* saved macro state */
    int was_defined;       /* 1 if macro existed when pushed */
};
static struct pp_macro_save pp_macro_stack[PP_MAX_MACRO_STACK];
static int pp_macro_stack_depth;

/* token buffer for expanded output */
static struct tok pp_tokbuf[PP_MAX_EXPAND];
static int pp_tokbuf_len;
static int pp_tokbuf_pos;

static int pp_at_bol; /* at beginning of line (for # detection) */
static int pp_cur_include_path_idx; /* index of -I path current file came from */

/* ---- command-line -D / -U storage ---- */
#define PP_MAX_CMD_DEFS    64
#define PP_MAX_CMD_UNDEFS  64

static const char *pp_cmd_defines[PP_MAX_CMD_DEFS];
static int pp_ncmd_defines;
static const char *pp_cmd_undefs[PP_MAX_CMD_UNDEFS];
static int pp_ncmd_undefs;

#define PP_MAX_CMD_FINC    16
static const char *pp_cmd_force_inc[PP_MAX_CMD_FINC];
static int pp_ncmd_force_inc;

/* ---- dependency tracking for -MD/-MMD ---- */
#define PP_MAX_DEPS 1024
static char *pp_dep_files[PP_MAX_DEPS];
static int pp_ndep_files;
static int pp_dep_exclude_system; /* 1 for -MMD (skip system headers) */

static void pp_dep_reset(void)
{
    int i;
    for (i = 0; i < pp_ndep_files; i++) {
        free(pp_dep_files[i]);
        pp_dep_files[i] = NULL;
    }
    pp_ndep_files = 0;
}

static void pp_record_dep(const char *filename, int is_system)
{
    int i;
    size_t len;
    char *copy;

    if (pp_dep_exclude_system && is_system) {
        return;
    }
    /* deduplicate */
    for (i = 0; i < pp_ndep_files; i++) {
        if (strcmp(pp_dep_files[i], filename) == 0) {
            return;
        }
    }
    if (pp_ndep_files < PP_MAX_DEPS) {
        /* malloc copy so it survives arena reset */
        len = strlen(filename);
        copy = (char *)malloc(len + 1);
        if (copy != NULL) {
            memcpy(copy, filename, len + 1);
            pp_dep_files[pp_ndep_files++] = copy;
        }
    }
}

void pp_dep_set_exclude_system(int exclude)
{
    pp_dep_exclude_system = exclude;
}

int pp_dep_get_count(void)
{
    return pp_ndep_files;
}

const char *pp_dep_get_file(int idx)
{
    if (idx < 0 || idx >= pp_ndep_files) {
        return NULL;
    }
    return pp_dep_files[idx];
}

void pp_dep_write(FILE *out, const char *target)
{
    int i;

    fprintf(out, "%s:", target);
    for (i = 0; i < pp_ndep_files; i++) {
        fprintf(out, " %s", pp_dep_files[i]);
    }
    fprintf(out, "\n");
}

/* ---- helpers ---- */

static struct pp_macro *pp_find_macro(const char *name)
{
    unsigned int h;
    int idx;

    h = pp_hash_str(name) & PP_HASH_MASK;
    idx = pp_hash_buckets[h];
    while (idx >= 0) {
        if (strcmp(pp_macros[idx].name, name) == 0) {
            return &pp_macros[idx];
        }
        idx = pp_macro_next[idx];
    }
    return NULL;
}

static struct pp_macro *pp_add_macro(const char *name)
{
    struct pp_macro *m;
    unsigned int h;
    int idx;

    /* check if already defined */
    h = pp_hash_str(name) & PP_HASH_MASK;
    idx = pp_hash_buckets[h];
    while (idx >= 0) {
        if (strcmp(pp_macros[idx].name, name) == 0) {
            /* redefine: clear but keep hash chain intact */
            {
                int saved_next;
                saved_next = pp_macro_next[idx];
                memset(&pp_macros[idx], 0, sizeof(pp_macros[0]));
                pp_macros[idx].name = str_dup(pp_arena, name,
                                              (int)strlen(name));
                pp_macro_next[idx] = saved_next;
            }
            return &pp_macros[idx];
        }
        idx = pp_macro_next[idx];
    }

    if (pp_nmacros >= PP_MAX_MACROS) {
        err("too many macro definitions");
        return NULL;
    }
    idx = pp_nmacros++;
    m = &pp_macros[idx];
    memset(m, 0, sizeof(*m));
    m->name = str_dup(pp_arena, name, (int)strlen(name));

    /* insert at head of hash chain */
    pp_macro_next[idx] = pp_hash_buckets[h];
    pp_hash_buckets[h] = idx;
    return m;
}

static void pp_remove_macro(const char *name)
{
    unsigned int h;
    int idx;
    int prev;

    h = pp_hash_str(name) & PP_HASH_MASK;
    prev = -1;
    idx = pp_hash_buckets[h];
    while (idx >= 0) {
        if (strcmp(pp_macros[idx].name, name) == 0) {
            /* remove from hash chain */
            if (prev >= 0) {
                pp_macro_next[prev] = pp_macro_next[idx];
            } else {
                pp_hash_buckets[h] = pp_macro_next[idx];
            }
            /* mark slot as empty by clearing the name */
            pp_macros[idx].name = "";
            pp_macro_next[idx] = -1;
            return;
        }
        prev = idx;
        idx = pp_macro_next[idx];
    }
}

/*
 * pp_define_num - define a simple numeric macro: #define name val
 */
static void pp_define_num(const char *name, long val)
{
    struct pp_macro *m;

    m = pp_add_macro(name);
    if (m == NULL) {
        return;
    }
    m->is_func = 0;
    m->body = (struct tok *)arena_alloc(pp_arena, sizeof(struct tok));
    m->body_cap = 1;
    memset(&m->body[0], 0, sizeof(struct tok));
    m->body[0].kind = TOK_NUM;
    m->body[0].val = val;
    m->body[0].file = "<builtin>";
    m->body_len = 1;
}

/*
 * pp_define_float - define a floating-point macro: #define name fval
 */
static void pp_define_float(const char *name, double fval)
{
    struct pp_macro *m;

    m = pp_add_macro(name);
    if (m == NULL) {
        return;
    }
    m->is_func = 0;
    m->body = (struct tok *)arena_alloc(pp_arena, sizeof(struct tok));
    m->body_cap = 1;
    memset(&m->body[0], 0, sizeof(struct tok));
    m->body[0].kind = TOK_FNUM;
    m->body[0].fval = fval;
    m->body[0].file = "<builtin>";
    m->body_len = 1;
}

/*
 * pp_define_str - define a string macro: #define name "val"
 */
static void pp_define_str(const char *name, const char *val)
{
    struct pp_macro *m;

    m = pp_add_macro(name);
    if (m == NULL) {
        return;
    }
    m->is_func = 0;
    m->body = (struct tok *)arena_alloc(pp_arena, sizeof(struct tok));
    m->body_cap = 1;
    memset(&m->body[0], 0, sizeof(struct tok));
    m->body[0].kind = TOK_STR;
    m->body[0].str = str_dup(pp_arena, val, (int)strlen(val));
    m->body[0].len = (int)strlen(val);
    m->body[0].file = "<builtin>";
    m->body_len = 1;
}

/*
 * pp_define_ident - define a macro that expands to an identifier token.
 * Used for macros like __BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__
 */
static void pp_define_ident(const char *name, const char *ident)
{
    struct pp_macro *m;
    int ident_len;
    enum tok_kind kw;

    m = pp_add_macro(name);
    if (m == NULL) {
        return;
    }
    m->is_func = 0;
    m->body = (struct tok *)arena_alloc(pp_arena, sizeof(struct tok));
    m->body_cap = 1;
    memset(&m->body[0], 0, sizeof(struct tok));

    /* check if the identifier is actually a keyword */
    ident_len = (int)strlen(ident);
    kw = lex_keyword_kind(ident, ident_len);

    m->body[0].kind = kw;
    m->body[0].str = str_dup(pp_arena, ident, ident_len);
    m->body[0].raw = m->body[0].str;
    m->body[0].len = ident_len;
    m->body[0].file = "<builtin>";
    m->body_len = 1;
}

/*
 * pp_define_kw1 - define a macro that expands to a single keyword token.
 * Used for type macros like __INT32_TYPE__ -> int.
 */
static void pp_define_kw1(const char *name,
                          enum tok_kind k1, const char *s1)
{
    struct pp_macro *m;

    m = pp_add_macro(name);
    if (m == NULL) {
        return;
    }
    m->is_func = 0;
    m->body = (struct tok *)arena_alloc(pp_arena, sizeof(struct tok));
    m->body_cap = 1;
    memset(&m->body[0], 0, sizeof(struct tok));
    m->body[0].kind = k1;
    m->body[0].str = s1 ? str_dup(pp_arena, s1, (int)strlen(s1)) : NULL;
    m->body[0].raw = m->body[0].str;
    m->body[0].len = s1 ? (int)strlen(s1) : 0;
    m->body[0].file = "<builtin>";
    m->body_len = 1;
}

/*
 * pp_define_kw2 - define a macro that expands to two keyword tokens.
 * Used for type macros like __SIZE_TYPE__ -> unsigned long.
 */
static void pp_define_kw2(const char *name,
                          enum tok_kind k1, const char *s1,
                          enum tok_kind k2, const char *s2)
{
    struct pp_macro *m;

    m = pp_add_macro(name);
    if (m == NULL) {
        return;
    }
    m->is_func = 0;
    m->body = (struct tok *)arena_alloc(pp_arena, 2 * sizeof(struct tok));
    m->body_cap = 2;
    memset(&m->body[0], 0, sizeof(struct tok));
    m->body[0].kind = k1;
    m->body[0].str = s1 ? str_dup(pp_arena, s1, (int)strlen(s1)) : NULL;
    m->body[0].raw = m->body[0].str;
    m->body[0].len = s1 ? (int)strlen(s1) : 0;
    m->body[0].file = "<builtin>";
    memset(&m->body[1], 0, sizeof(struct tok));
    m->body[1].kind = k2;
    m->body[1].str = s2 ? str_dup(pp_arena, s2, (int)strlen(s2)) : NULL;
    m->body[1].raw = m->body[1].str;
    m->body[1].len = s2 ? (int)strlen(s2) : 0;
    m->body[1].file = "<builtin>";
    m->body_len = 2;
}

/*
 * pp_define_empty - define a macro with no body: #define name
 */
static void pp_define_empty(const char *name)
{
    struct pp_macro *m;

    m = pp_add_macro(name);
    if (m == NULL) {
        return;
    }
    m->is_func = 0;
    m->body = NULL;
    m->body_cap = 0;
    m->body_len = 0;
}

/*
 * pp_define_cmdline - process a -D name[=val] definition.
 * Called from cc.c after pp_init().
 */
void pp_define_cmdline(const char *def)
{
    const char *eq;
    char name_buf[256];
    int nlen;
    long val;

    eq = strchr(def, '=');
    if (eq == NULL) {
        /* -Dfoo => #define foo 1 */
        pp_define_num(def, 1);
    } else {
        nlen = (int)(eq - def);
        if (nlen >= (int)sizeof(name_buf)) {
            nlen = (int)sizeof(name_buf) - 1;
        }
        memcpy(name_buf, def, (size_t)nlen);
        name_buf[nlen] = '\0';

        /* try to parse as a number */
        val = strtol(eq + 1, NULL, 0);
        if (val != 0 || (eq[1] == '0' && eq[2] == '\0')) {
            pp_define_num(name_buf, val);
        } else if (eq[1] == '\0') {
            /* -Dfoo= => #define foo (empty) */
            pp_define_empty(name_buf);
        } else if (eq[1] == '"') {
            /* -Dfoo="string" => #define foo "string" */
            const char *sv;
            int slen;
            struct pp_macro *sm;
            sv = eq + 2;  /* skip opening quote */
            slen = (int)strlen(sv);
            if (slen > 0 && sv[slen - 1] == '"') slen--; /* strip closing */
            sm = pp_add_macro(name_buf);
            if (sm != NULL) {
                sm->is_func = 0;
                sm->body = (struct tok *)arena_alloc(pp_arena,
                    sizeof(struct tok));
                sm->body_cap = 1;
                memset(&sm->body[0], 0, sizeof(struct tok));
                sm->body[0].kind = TOK_STR;
                sm->body[0].str = str_dup(pp_arena, sv, slen);
                sm->body[0].raw = NULL;
                sm->body[0].len = slen;
                sm->body[0].file = "<cmdline>";
                sm->body_len = 1;
            }
        } else {
            /* define as identifier token */
            pp_define_ident(name_buf, eq + 1);
        }
    }
}

/*
 * pp_undef_cmdline - process a -U name undefinition.
 */
void pp_undef_cmdline(const char *name)
{
    pp_remove_macro(name);
}

/*
 * pp_add_cmdline_define - register a -D define to apply after pp_init.
 */
void pp_add_cmdline_define(const char *def)
{
    if (pp_ncmd_defines < PP_MAX_CMD_DEFS) {
        pp_cmd_defines[pp_ncmd_defines++] = def;
    }
}

/*
 * pp_add_cmdline_undef - register a -U undef to apply after pp_init.
 */
void pp_add_cmdline_undef(const char *name)
{
    if (pp_ncmd_undefs < PP_MAX_CMD_UNDEFS) {
        pp_cmd_undefs[pp_ncmd_undefs++] = name;
    }
}

void pp_add_force_include(const char *path)
{
    if (pp_ncmd_force_inc < PP_MAX_CMD_FINC) {
        pp_cmd_force_inc[pp_ncmd_force_inc++] = path;
    }
}

static int pp_cond_active(void)
{
    if (pp_cond_depth == 0) {
        return 1;
    }
    return pp_cond_stack[pp_cond_depth - 1].active;
}

static int pp_is_pragma_once(const char *filename)
{
    unsigned int h;
    struct pp_once_entry *e;

    h = pp_hash_str(filename) & PP_ONCE_HASH_MASK;
    e = pp_once_buckets[h];
    while (e != NULL) {
        if (strcmp(e->filename, filename) == 0) {
            return 1;
        }
        e = e->next;
    }
    return 0;
}

static void pp_mark_pragma_once(const char *filename)
{
    unsigned int h;
    struct pp_once_entry *e;

    if (pp_is_pragma_once(filename)) {
        return;
    }
    if (pp_nonce_pool >= PP_MAX_PRAGMA) {
        return;
    }
    h = pp_hash_str(filename) & PP_ONCE_HASH_MASK;
    e = &pp_once_pool[pp_nonce_pool++];
    e->filename = str_dup(pp_arena, filename, (int)strlen(filename));
    e->next = pp_once_buckets[h];
    pp_once_buckets[h] = e;
}

/* ---- include guard cache ---- */

static int pp_check_guard_cache(const char *filename)
{
    unsigned int h;
    struct pp_guard_entry *e;

    h = pp_hash_str(filename) & PP_GUARD_HASH_MASK;
    e = &pp_guard_cache[h];
    /* linear probing in case of collision */
    if (e->filename != NULL && strcmp(e->filename, filename) == 0) {
        /* file has a guard macro; check if it's still defined */
        if (e->guard_macro != NULL
            && pp_find_macro(e->guard_macro) != NULL) {
            return 1; /* skip this include */
        }
    }
    return 0;
}

static void pp_add_guard_cache(const char *filename, const char *guard)
{
    unsigned int h;
    struct pp_guard_entry *e;

    h = pp_hash_str(filename) & PP_GUARD_HASH_MASK;
    e = &pp_guard_cache[h];
    /* only store if slot is empty or same file (update) */
    if (e->filename == NULL
        || strcmp(e->filename, filename) == 0) {
        e->filename = str_dup(pp_arena, filename,
                              (int)strlen(filename));
        e->guard_macro = str_dup(pp_arena, guard,
                                 (int)strlen(guard));
    }
}

/*
 * pp_detect_include_guard - scan file content to detect
 * #ifndef GUARD / #define GUARD pattern at the start.
 * Returns the guard macro name, or NULL if not detected.
 */
static const char *pp_detect_include_guard(const char *content)
{
    const char *p;
    char guard[256];
    char def_name[256];
    int len;
    int dlen;

    p = content;

    /* skip leading whitespace and comments */
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        /* skip C-style comments */
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p != '\0') {
                if (p[0] == '*' && p[1] == '/') {
                    p += 2;
                    break;
                }
                p++;
            }
            continue;
        }
        /* skip C++ style comments */
        if (p[0] == '/' && p[1] == '/') {
            while (*p != '\0' && *p != '\n') {
                p++;
            }
            continue;
        }
        break;
    }

    /* expect #ifndef or #if !defined(...) */
    if (*p != '#') {
        return NULL;
    }
    p++;
    while (*p == ' ' || *p == '\t') p++;

    /* check for "ifndef" */
    if (strncmp(p, "ifndef", 6) != 0) {
        return NULL;
    }
    p += 6;
    if (*p != ' ' && *p != '\t') {
        return NULL;
    }
    while (*p == ' ' || *p == '\t') p++;

    /* read guard name */
    len = 0;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
           || (*p >= '0' && *p <= '9') || *p == '_') {
        if (len < (int)sizeof(guard) - 1) {
            guard[len++] = *p;
        }
        p++;
    }
    guard[len] = '\0';
    if (len == 0) {
        return NULL;
    }

    /* skip to next line */
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (*p == '\n') p++;
    else return NULL;

    /* skip whitespace */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    /* expect #define GUARD */
    if (*p != '#') {
        return NULL;
    }
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "define", 6) != 0) {
        return NULL;
    }
    p += 6;
    if (*p != ' ' && *p != '\t') {
        return NULL;
    }
    while (*p == ' ' || *p == '\t') p++;

    /* read the define name */
    dlen = 0;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
           || (*p >= '0' && *p <= '9') || *p == '_') {
        if (dlen < (int)sizeof(def_name) - 1) {
            def_name[dlen++] = *p;
        }
        p++;
    }
    def_name[dlen] = '\0';

    /* guard name must match */
    if (dlen != len || strcmp(guard, def_name) != 0) {
        return NULL;
    }

    return str_dup(pp_arena, guard, len);
}

/* ---- raw token reading (no macro expansion, stays on one line for pp) ---- */

static struct tok *pp_raw_next(void)
{
    return lex_next();
}

/* pp_skip_bsnl - advance p past any backslash-newline sequences.
 * Counts the line increments in *extra_lines. */
static const char *pp_skip_bsnl(const char *p, int *extra_lines)
{
    while (p[0] == '\\' && p[1] == '\n') {
        p += 2;
        if (extra_lines != NULL) {
            (*extra_lines)++;
        }
    }
    return p;
}

/* skip to end of logical line (handling backslash-newline continuations) */
static void pp_skip_line(void)
{
    const char *p;
    int lines;

    lines = 0;
    p = lex_get_pos();
    for (;;) {
        p = pp_skip_bsnl(p, &lines);
        if (*p == '\0' || *p == '\n') {
            break;
        }
        p++;
    }
    if (*p == '\n') {
        p++;
        lines++;
    }
    lex_set_pos(p, lex_get_line() + lines, 1);
}

/* read rest of logical line as text, return it (joining continuations) */
static char *pp_read_line_text(void)
{
    const char *p;
    char buf[PP_BUF_SIZE];
    int len;
    int lines;
    char *result;

    p = lex_get_pos();
    lines = 0;
    len = 0;

    /* skip leading spaces */
    for (;;) {
        p = pp_skip_bsnl(p, &lines);
        if (*p != ' ' && *p != '\t') {
            break;
        }
        p++;
    }

    /* read logical line content, joining continuations */
    while (*p != '\0') {
        p = pp_skip_bsnl(p, &lines);
        if (*p == '\n' || *p == '\0') {
            break;
        }
        if (len < (int)sizeof(buf) - 1) {
            buf[len++] = *p;
        }
        p++;
    }

    /* trim trailing whitespace */
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t'
                     || buf[len - 1] == '\r')) {
        len--;
    }
    buf[len] = '\0';
    result = str_dup(pp_arena, buf, len);

    if (*p == '\n') {
        p++;
        lines++;
    }
    lex_set_pos(p, lex_get_line() + lines, 1);
    return result;
}

/* read tokens until end of logical line, storing them in an array.
 * Handles backslash-newline continuations.
 * Returns the number of tokens read. */
static int pp_read_line_tokens(struct tok *out, int max)
{
    int n;
    const char *p;
    struct tok *t;
    int extra_lines;

    n = 0;
    (void)lex_get_line();

    for (;;) {
        /* check if we've crossed to a new line at the source level */
        p = lex_get_pos();
        extra_lines = 0;
        /* skip spaces, comments, and backslash-newline continuations
         * (but not newlines) to detect end of directive line */
        for (;;) {
            p = pp_skip_bsnl(p, &extra_lines);
            if (*p == ' ' || *p == '\t' || *p == '\r') {
                p++;
            } else if (*p == '/' && *(p + 1) == '*') {
                /* skip block comment */
                p += 2;
                while (*p != '\0') {
                    if (*p == '\n') {
                        extra_lines++;
                    }
                    if (*p == '*' && *(p + 1) == '/') {
                        p += 2;
                        break;
                    }
                    p++;
                }
            } else if (*p == '/' && *(p + 1) == '/') {
                /* skip line comment to end of line */
                p += 2;
                while (*p != '\0' && *p != '\n') {
                    p++;
                }
            } else {
                break;
            }
        }
        if (*p == '\n' || *p == '\0') {
            /* end of directive logical line */
            if (*p == '\n') {
                /* advance past newline */
                lex_set_pos(p + 1, lex_get_line() + extra_lines + 1, 1);
            } else {
                if (extra_lines > 0) {
                    lex_set_pos(p, lex_get_line() + extra_lines, 1);
                }
            }
            break;
        }

        /* if we skipped continuations, update lexer position */
        if (extra_lines > 0) {
            lex_set_pos(p, lex_get_line() + extra_lines, 1);
        }

        t = pp_raw_next();
        if (t->kind == TOK_EOF) {
            break;
        }
        if (n < max) {
            out[n++] = *t;
        }
    }
    return n;
}

/* ---- file reading ---- */

static char *pp_read_file(const char *path)
{
    FILE *f;
    long sz;
    char *buf;
    size_t nread;

    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = (char *)arena_alloc(pp_arena, (usize)(sz + 1));
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    nread = fread(buf, 1, (size_t)sz, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

/*
 * pp_find_include_from - search for an include file starting from a given
 * include-path index. Sets *found_idx to the index of the path that matched,
 * or -1 if found relative to the current file or not at all.
 */
static char *pp_find_include_from(const char *name, int is_system,
                                  int start_idx, int *found_idx,
                                  char *resolved_path, int resolved_path_sz)
{
    char path[4096];
    char *content;
    int i;
    const char *cur_file;
    const char *slash;
    int dirlen;

    if (found_idx != NULL) {
        *found_idx = -1;
    }
    if (resolved_path != NULL && resolved_path_sz > 0) {
        resolved_path[0] = '\0';
    }

    /* for quoted includes, try relative to current file first */
    if (!is_system && start_idx <= 0) {
        cur_file = lex_get_filename();
        slash = strrchr(cur_file, '/');
        if (slash != NULL) {
            dirlen = (int)(slash - cur_file + 1);
            if (dirlen + (int)strlen(name) < (int)sizeof(path)) {
                memcpy(path, cur_file, dirlen);
                strcpy(path + dirlen, name);
                content = pp_read_file(path);
                if (content != NULL) {
                    if (resolved_path != NULL && resolved_path_sz > 0) {
                        strcpy(resolved_path, path);
                    }
                    return content;
                }
            }
        }
        /* try current directory */
        content = pp_read_file(name);
        if (content != NULL) {
            if (resolved_path != NULL && resolved_path_sz > 0) {
                strcpy(resolved_path, name);
            }
            return content;
        }
    }

    /* search include paths, starting from start_idx */
    for (i = (start_idx > 0 ? start_idx : 0); i < pp_ninclude_paths; i++) {
        dirlen = (int)strlen(pp_include_paths[i]);
        if (dirlen + 1 + (int)strlen(name) < (int)sizeof(path)) {
            memcpy(path, pp_include_paths[i], dirlen);
            path[dirlen] = '/';
            strcpy(path + dirlen + 1, name);
            content = pp_read_file(path);
            if (content != NULL) {
                if (found_idx != NULL) {
                    *found_idx = i;
                }
                if (resolved_path != NULL && resolved_path_sz > 0) {
                    strcpy(resolved_path, path);
                }
                return content;
            }
        }
    }

    return NULL;
}

/* pp_find_include - unused convenience wrapper */
#if 0
static char *pp_find_include(const char *name, int is_system)
{
    return pp_find_include_from(name, is_system, 0, NULL, NULL, 0);
}
#endif

/* ---- include path management ---- */

void pp_add_include_path(const char *path)
{
    if (pp_ninclude_paths < PP_MAX_PATHS) {
        /* store as-is; path pointers from argv live for the process */
        pp_include_paths[pp_ninclude_paths++] = (char *)path;
    }
}

/* ---- push/pop include ---- */

static void pp_push_include_ex(const char *src, const char *filename,
                               int path_idx)
{
    struct pp_include_frame *fr;

    if (pp_inc_depth >= PP_MAX_INCLUDE) {
        err("include nesting too deep");
        return;
    }

    /* save current state */
    fr = &pp_inc_stack[pp_inc_depth++];
    fr->pos = lex_get_pos();
    fr->filename = lex_get_filename();
    fr->line = lex_get_line();
    fr->col = lex_get_col();
    fr->src = NULL; /* the lex state captures source implicitly */
    fr->include_path_idx = pp_cur_include_path_idx;

    /* set new include path index and start lexing the new file */
    pp_cur_include_path_idx = path_idx;
    lex_init(src, filename, pp_arena);
}

static void pp_push_include(const char *src, const char *filename)
{
    pp_push_include_ex(src, filename, -1);
}

static int pp_pop_include(void)
{
    struct pp_include_frame *fr;

    if (pp_inc_depth == 0) {
        return 0;
    }

    pp_inc_depth--;
    fr = &pp_inc_stack[pp_inc_depth];

    /* restore lexer state */
    lex_init(fr->pos, fr->filename, pp_arena);
    /* we need to set position to where we left off, but lex_init starts
     * at the beginning of the string. The pos IS the remaining source. */
    lex_set_pos(fr->pos, fr->line, fr->col);
    lex_set_filename(fr->filename);
    pp_cur_include_path_idx = fr->include_path_idx;

    return 1;
}

/* ---- stringify ---- */

static void tok_copy_text(char *buf, int bufsz, const char *text)
{
    int i;

    if (bufsz <= 0) {
        return;
    }
    if (text == NULL) {
        buf[0] = '\0';
        return;
    }

    i = 0;
    while (text[i] != '\0' && i < bufsz - 1) {
        buf[i] = text[i];
        i++;
    }
    buf[i] = '\0';
}

static void tok_to_str(struct tok *t, char *buf, int bufsz)
{
    int n;

    n = 0;
    if (bufsz <= 0) {
        return;
    }
    if (t->raw != NULL) {
        tok_copy_text(buf, bufsz, t->raw);
        return;
    }
    switch (t->kind) {
    case TOK_NUM:
        sprintf(buf, "%ld", t->val);
        (void)bufsz;
        break;
    case TOK_FNUM:
        sprintf(buf, "%g", t->fval);
        (void)bufsz;
        break;
    case TOK_IDENT:
    case TOK_STR:
        if (t->str != NULL) {
            if (t->kind == TOK_STR) {
                sprintf(buf, "\"%s\"", t->str);
            } else {
                tok_copy_text(buf, bufsz, t->str);
            }
        } else {
            buf[0] = '\0';
        }
        break;
    case TOK_CHAR_LIT:
        sprintf(buf, "'%c'", (char)t->val);
        break;
    default:
        /* reconstruct operator text */
        {
            static const char *op_strs[] = {
                "0","0.0","id","str","chr",
                "+","-","*","/","%",
                "&","|","^","~",
                "<<",">>",
                "&&","||","!",
                "==","!=","<",">","<=",">=",
                "=","+=","-=","*=","/=",
                "%=","&=","|=","^=",
                "<<=",">>=",
                ";",",",".","-\x3e","...",
                "(",")","{","}",
                "[","]",
                "?",":","#","##",
                "++","--"
            };
            int idx = (int)t->kind;
            if (idx >= 0 && idx < (int)(sizeof(op_strs)/sizeof(op_strs[0]))) {
                tok_copy_text(buf, bufsz, op_strs[idx]);
            } else if (t->str) {
                tok_copy_text(buf, bufsz, t->str);
            } else {
                buf[0] = '\0';
            }
        }
        break;
    }
    (void)n;
}

static struct tok stringify_tokens(struct tok *toks, int ntoks)
{
    char buf[4096];
    int pos;
    int i;
    char tmp[256];
    struct tok result;
    int prev_line;
    int prev_end_col;
    int need_space;
    int tok_len;

    pos = 0;
    prev_line = 0;
    prev_end_col = 0;
    for (i = 0; i < ntoks && pos < (int)sizeof(buf) - 2; i++) {
        tok_to_str(&toks[i], tmp, sizeof(tmp));
        tok_len = (int)strlen(tmp);
        need_space = 0;
        if (i > 0) {
            /* Keep the original token adjacency when we can infer it from
             * source locations; this avoids breaking asm macro text such as
             * .L__gpr_num_\rt or \sreg. */
            if (toks[i].line > prev_line) {
                need_space = 1;
            } else if (toks[i].line == prev_line &&
                       toks[i].col > prev_end_col + 1) {
                need_space = 1;
            }
        }
        if (need_space && pos < (int)sizeof(buf) - 2) {
            buf[pos++] = ' ';
        }
        {
            int k = 0;
            while (tmp[k] && pos < (int)sizeof(buf) - 2) {
                buf[pos++] = tmp[k++];
            }
        }
        prev_line = toks[i].line;
        prev_end_col = toks[i].col + tok_len - 1;
    }
    buf[pos] = '\0';

    memset(&result, 0, sizeof(result));
    result.kind = TOK_STR;
    result.str = str_dup(pp_arena, buf, pos);
    result.len = pos;
    result.file = lex_get_filename();
    result.line = lex_get_line();
    result.col = lex_get_col();
    return result;
}

/* ---- token pasting ---- */

static struct tok paste_tokens(struct tok *a, struct tok *b)
{
    char buf[512];
    char tmp1[256];
    char tmp2[256];
    struct tok result;
    int len;

    tok_to_str(a, tmp1, sizeof(tmp1));
    tok_to_str(b, tmp2, sizeof(tmp2));
    strcpy(buf, tmp1);
    strcat(buf, tmp2);
    len = (int)strlen(buf);

    /* re-lex the pasted result */
    memset(&result, 0, sizeof(result));
    /* simple heuristic: if it looks like a number, treat as number;
     * otherwise treat as identifier */
    if (buf[0] >= '0' && buf[0] <= '9') {
        long v = 0;
        int i;
        for (i = 0; buf[i] >= '0' && buf[i] <= '9'; i++) {
            v = v * 10 + (buf[i] - '0');
        }
        result.kind = TOK_NUM;
        result.val = v;
    } else {
        result.kind = TOK_IDENT;
        result.str = str_dup(pp_arena, buf, len);
    }
    result.raw = str_dup(pp_arena, buf, len);
    result.len = len;
    result.file = a->file;
    result.line = a->line;
    result.col = a->col;
    return result;
}

/* ---- macro expansion workspace pool ---- */
/* Pre-allocate a pool of expansion buffers to avoid malloc/free
 * on every macro expansion.  Maximum recursion depth = pool size. */
#define PP_EXPAND_POOL_SIZE 8
static struct tok pp_expand_pool[PP_EXPAND_POOL_SIZE][PP_MAX_BODY];
static int pp_expand_pool_top;

static struct tok *pp_expand_pool_get(void)
{
    if (pp_expand_pool_top < PP_EXPAND_POOL_SIZE) {
        return pp_expand_pool[pp_expand_pool_top++];
    }
    /* fallback to malloc if pool exhausted */
    return (struct tok *)malloc((usize)PP_MAX_BODY * sizeof(struct tok));
}

static void pp_expand_pool_put(struct tok *buf)
{
    if (pp_expand_pool_top > 0
        && buf == pp_expand_pool[pp_expand_pool_top - 1]) {
        pp_expand_pool_top--;
    } else {
        /* was malloc'd fallback */
        free(buf);
    }
}

/* ---- macro expansion ---- */

static void pp_expand_macro(struct pp_macro *m, struct tok *args[],
                            int nargs[], int narg_lists,
                            struct tok *out, int *out_len, int out_max);

/* expand all macros in a token sequence, appending results to out */
static void pp_expand_tokens(struct tok *in, int nin,
                             struct tok *out, int *out_len, int out_max)
{
    int i;
    struct pp_macro *m;
    struct tok *args[PP_MAX_PARAMS];
    int nargs[PP_MAX_PARAMS];
    int narg_lists;
    int depth;
    int j;
    int arg_start;
    int arg_n;

    i = 0;
    while (i < nin) {
        /* Skip 'defined' operator and its argument to prevent
         * macro expansion inside defined() in #if expressions.
         * defined(X) and defined X must not expand X. */
        if (pp_tok_is_name(&in[i]) &&
            strcmp(in[i].str, "defined") == 0) {
            /* copy 'defined' */
            if (*out_len < out_max) {
                out[(*out_len)++] = in[i];
            }
            i++;
            /* copy optional '(' */
            if (i < nin && in[i].kind == TOK_LPAREN) {
                if (*out_len < out_max) {
                    out[(*out_len)++] = in[i];
                }
                i++;
            }
            /* copy the identifier argument (may be a keyword token) */
            if (i < nin && pp_tok_is_name(&in[i])) {
                if (*out_len < out_max) {
                    out[(*out_len)++] = in[i];
                }
                i++;
            }
            /* copy optional ')' */
            if (i < nin && in[i].kind == TOK_RPAREN) {
                if (*out_len < out_max) {
                    out[(*out_len)++] = in[i];
                }
                i++;
            }
            continue;
        }
        if (pp_tok_is_name(&in[i])) {
            /* handle __LINE__ and __FILE__ in expansion context */
            if (strcmp(in[i].str, "__LINE__") == 0) {
                struct tok lt;
                memset(&lt, 0, sizeof(lt));
                lt.kind = TOK_NUM;
                lt.val = in[i].line > 0 ? in[i].line
                         : lex_get_line();
                lt.file = in[i].file;
                lt.line = in[i].line;
                lt.col = in[i].col;
                if (*out_len < out_max) {
                    out[(*out_len)++] = lt;
                }
                i++;
                continue;
            }
            if (strcmp(in[i].str, "__FILE__") == 0) {
                struct tok ft;
                const char *fn2;
                fn2 = lex_get_filename();
                memset(&ft, 0, sizeof(ft));
                ft.kind = TOK_STR;
                ft.str = str_dup(pp_arena, fn2, (int)strlen(fn2));
                ft.len = (int)strlen(fn2);
                ft.file = in[i].file;
                ft.line = in[i].line;
                ft.col = in[i].col;
                if (*out_len < out_max) {
                    out[(*out_len)++] = ft;
                }
                i++;
                continue;
            }
            m = pp_find_macro(in[i].str);
            if (m != NULL && (m->disabled || in[i].no_expand)) {
                /* "painted blue" - output the token but mark it
                 * so it won't be re-expanded later */
                if (*out_len < out_max) {
                    out[*out_len] = in[i];
                    out[*out_len].no_expand = 1;
                    (*out_len)++;
                }
                i++;
                continue;
            }
            if (m != NULL) {
                if (m->is_func) {
                    /* need '(' after macro name */
                    if (i + 1 < nin && in[i + 1].kind == TOK_LPAREN) {
                        /* collect arguments */
                        narg_lists = 0;
                        j = i + 2; /* skip name and '(' */
                        depth = 1;
                        arg_start = j;
                        arg_n = 0;
                        while (j < nin && depth > 0) {
                            if (in[j].kind == TOK_LPAREN) {
                                depth++;
                            } else if (in[j].kind == TOK_RPAREN) {
                                depth--;
                                if (depth == 0) {
                                    if (arg_n > 0 || narg_lists > 0
                                        || j > arg_start) {
                                        args[narg_lists] = &in[arg_start];
                                        nargs[narg_lists] = j - arg_start;
                                        narg_lists++;
                                    }
                                    break;
                                }
                            } else if (in[j].kind == TOK_COMMA && depth == 1
                                       && !(m->is_variadic && narg_lists >= m->nparams)) {
                                args[narg_lists] = &in[arg_start];
                                nargs[narg_lists] = j - arg_start;
                                narg_lists++;
                                arg_start = j + 1;
                            }
                            j++;
                        }
                        i = j + 1; /* skip past ')' */
                        pp_expand_macro(m, args, nargs, narg_lists,
                                        out, out_len, out_max);
                        continue;
                    }
                    /* no '(' follows: not a macro invocation */
                } else {
                    /* object-like macro */
                    {
                        int prev_len;
                        prev_len = *out_len;
                        i++;
                        pp_expand_macro(m, NULL, NULL, 0,
                                        out, out_len, out_max);
                        /* If the expansion ends with a
                         * function-like macro name and the
                         * next input token is '(', combine
                         * them into a function invocation.
                         * E.g. #define api_check luai_apicheck
                         *      api_check(L, 1)
                         * expands api_check to luai_apicheck,
                         * then must recognize luai_apicheck(
                         * as a macro call. */
                        if (*out_len > prev_len
                            && pp_tok_is_name(&out[*out_len - 1])
                            && i < nin
                            && in[i].kind == TOK_LPAREN) {
                            struct pp_macro *m2;
                            m2 = pp_find_macro(
                                out[*out_len - 1].str);
                            if (m2 != NULL && !m2->disabled
                                && m2->is_func) {
                                /* remove the macro name from
                                 * output */
                                (*out_len)--;
                                /* collect arguments from in[]
                                 * starting after '(' */
                                narg_lists = 0;
                                j = i + 1;
                                depth = 1;
                                arg_start = j;
                                while (j < nin && depth > 0) {
                                    if (in[j].kind
                                        == TOK_LPAREN) {
                                        depth++;
                                    } else if (in[j].kind
                                               == TOK_RPAREN) {
                                        depth--;
                                        if (depth == 0) {
                                            if (narg_lists > 0
                                                || j
                                                   > arg_start) {
                                                args[narg_lists]
                                                    = &in[
                                                    arg_start];
                                                nargs[narg_lists]
                                                    = j
                                                      - arg_start;
                                                narg_lists++;
                                            }
                                            break;
                                        }
                                    } else if (in[j].kind
                                               == TOK_COMMA
                                               && depth == 1
                                               && !(m2->is_variadic
                                                    && narg_lists
                                                       >= m2->nparams)) {
                                        args[narg_lists] =
                                            &in[arg_start];
                                        nargs[narg_lists] =
                                            j - arg_start;
                                        narg_lists++;
                                        arg_start = j + 1;
                                    }
                                    j++;
                                }
                                i = j + 1;
                                pp_expand_macro(
                                    m2, args, nargs,
                                    narg_lists,
                                    out, out_len, out_max);
                                continue;
                            }
                        }
                        continue;
                    }
                }
            }
        }
        /* keyword tokens that have been #define'd */
        if (in[i].kind >= TOK_AUTO && in[i].kind <= TOK_COMPLEX
            && in[i].str != NULL && in[i].len > 0) {
            m = pp_find_macro(in[i].str);
            if (m != NULL && !m->disabled && !m->is_func) {
                i++;
                pp_expand_macro(m, NULL, NULL, 0,
                                out, out_len, out_max);
                continue;
            }
        }
        /* not a macro, just copy */
        if (*out_len < out_max) {
            out[(*out_len)++] = in[i];
        }
        i++;
    }
}

/* pp_resolve_param - if tok is an identifier matching a macro parameter,
 * return the parameter index; otherwise return -1.
 * For variadic macros, __VA_ARGS__ maps to the parameter index == nparams
 * (the variadic slot). */
static int pp_resolve_param(struct pp_macro *m, struct tok *t)
{
    int j;

    if (!m->is_func || !pp_tok_is_name(t)) {
        return -1;
    }
    for (j = 0; j < m->nparams; j++) {
        if (strcmp(t->str, m->params[j]) == 0) {
            return j;
        }
    }
    /* __VA_ARGS__ in a variadic macro maps to the variadic slot */
    if (m->is_variadic && strcmp(t->str, "__VA_ARGS__") == 0) {
        return m->nparams;  /* variadic args slot */
    }
    /* GNU named variadic param (e.g. "args" in (fmt, args...)) */
    if (m->is_variadic && m->va_name != NULL
        && strcmp(t->str, m->va_name) == 0) {
        return m->nparams;  /* variadic args slot */
    }
    return -1;
}

/* pp_is_va_args - check if token refers to variadic args
 * (__VA_ARGS__ or GNU named variadic param) */
static int pp_is_va_args(struct pp_macro *m, struct tok *t)
{
    if (!m->is_variadic || !pp_tok_is_name(t)) return 0;
    if (strcmp(t->str, "__VA_ARGS__") == 0) return 1;
    if (m->va_name != NULL && strcmp(t->str, m->va_name) == 0) return 1;
    return 0;
}

static void pp_expand_macro(struct pp_macro *m, struct tok *args[],
                            int nargs[], int narg_lists,
                            struct tok *out, int *out_len, int out_max)
{
    struct tok *expanded;
    int nexpanded;
    int i;
    int param_idx;
    int j;
    struct tok stringified;

    /* use pre-allocated pool to avoid malloc/free overhead */
    expanded = pp_expand_pool_get();
    if (expanded == NULL) {
        return;
    }

    /* NOTE: m->disabled is set to 1 only for the rescan phase below.
     * Per the C standard, macro arguments are fully expanded (including
     * recursive calls to the same macro) BEFORE substitution into the
     * body.  The macro is only "painted blue" during the final rescan
     * of the substituted body. */

    nexpanded = 0;
    i = 0;
    while (i < m->body_len) {
        /* token paste ## */
        if (i + 1 < m->body_len
            && m->body[i + 1].kind == TOK_PASTE) {

            /* GNU ## __VA_ARGS__ comma swallowing */
            if (i + 2 < m->body_len
                && pp_is_va_args(m, &m->body[i + 2])) {
                int va_idx;
                int va_empty;
                va_idx = m->nparams;
                va_empty = (va_idx >= narg_lists)
                           || (nargs[va_idx] == 0);
                if (va_empty) {
                    /* Check for chained paste:
                     * prefix ## __VA_ARGS__ ## suffix
                     * With empty VA_ARGS, paste prefix
                     * directly with suffix. */
                    if (i + 4 < m->body_len
                        && m->body[i + 3].kind == TOK_PASTE) {
                        /* paste body[i] ## body[i+4],
                         * skipping empty VA_ARGS */
                        struct tok left_tk;
                        struct tok right_tk;
                        struct tok pasted;
                        int lp;
                        int rp;
                        left_tk = m->body[i];
                        lp = pp_resolve_param(m, &left_tk);
                        if (lp >= 0 && lp < narg_lists
                            && nargs[lp] > 0) {
                            left_tk = args[lp]
                                [nargs[lp] - 1];
                        }
                        right_tk = m->body[i + 4];
                        rp = pp_resolve_param(m, &right_tk);
                        if (rp >= 0 && rp < narg_lists
                            && nargs[rp] > 0) {
                            right_tk = args[rp][0];
                        }
                        pasted = paste_tokens(&left_tk,
                                              &right_tk);
                        i += 5;
                        /* handle further chained paste */
                        while (i < m->body_len
                               && m->body[i].kind
                                  == TOK_PASTE
                               && i + 1 < m->body_len) {
                            right_tk = m->body[i + 1];
                            rp = pp_resolve_param(
                                m, &right_tk);
                            if (rp >= 0 && rp < narg_lists
                                && nargs[rp] > 0)
                                right_tk = args[rp][0];
                            pasted = paste_tokens(&pasted,
                                                  &right_tk);
                            i += 2;
                        }
                        if (nexpanded < PP_MAX_BODY)
                            expanded[nexpanded++] = pasted;
                        continue;
                    }
                    if (nexpanded > 0
                        && expanded[nexpanded - 1].kind
                           == TOK_COMMA) {
                        nexpanded--;
                    }
                    i += 3;
                    continue;
                }
                /* non-empty: the GNU comma swallowing ##
                 * just means "keep the comma and emit VA
                 * args normally".  But if the left side is
                 * NOT a comma (e.g. prefix##__VA_ARGS__),
                 * we must actually paste left with the
                 * first VA token.  Also handle chained
                 * paste: prefix##__VA_ARGS__##suffix. */
                {
                    struct tok ltk;
                    int va_n;
                    int is_comma_swallow;
                    ltk = m->body[i];
                    param_idx = pp_resolve_param(m, &ltk);
                    if (param_idx >= 0
                        && param_idx < narg_lists
                        && nargs[param_idx] > 0) {
                        ltk = args[param_idx]
                                  [nargs[param_idx] - 1];
                    }
                    va_n = (va_idx < narg_lists)
                           ? nargs[va_idx] : 0;
                    is_comma_swallow =
                        (m->body[i].kind == TOK_COMMA
                         || ltk.kind == TOK_COMMA);
                    if (is_comma_swallow) {
                        /* Standard , ## __VA_ARGS__: keep
                         * comma and emit args normally */
                        if (nexpanded < PP_MAX_BODY)
                            expanded[nexpanded++] = ltk;
                        for (j = 0; j < va_n
                             && nexpanded < PP_MAX_BODY;
                             j++)
                            expanded[nexpanded++] =
                                args[va_idx][j];
                        i += 3;
                        continue;
                    }
                    /* Non-comma left: actually paste
                     * prefix ## first_va_token */
                    if (va_n > 0) {
                        struct tok pasted;
                        pasted = paste_tokens(&ltk,
                            &args[va_idx][0]);
                        if (nexpanded < PP_MAX_BODY)
                            expanded[nexpanded++] = pasted;
                        /* emit middle VA arg tokens */
                        for (j = 1; j < va_n - 1
                             && nexpanded < PP_MAX_BODY;
                             j++)
                            expanded[nexpanded++] =
                                args[va_idx][j];
                        /* handle chained paste:
                         * prefix##VA##suffix */
                        if (i + 4 < m->body_len
                            && m->body[i + 3].kind
                               == TOK_PASTE) {
                            struct tok last_va;
                            struct tok right_tk;
                            struct tok p2;
                            int rp;
                            last_va = args[va_idx][va_n - 1];
                            if (va_n == 1) {
                                last_va = pasted;
                                if (nexpanded > 0)
                                    nexpanded--;
                            }
                            right_tk = m->body[i + 4];
                            rp = pp_resolve_param(
                                m, &right_tk);
                            if (rp >= 0 && rp < narg_lists
                                && nargs[rp] > 0)
                                right_tk = args[rp][0];
                            p2 = paste_tokens(&last_va,
                                              &right_tk);
                            if (nexpanded < PP_MAX_BODY)
                                expanded[nexpanded++] = p2;
                            i += 5;
                            continue;
                        }
                        if (va_n > 1
                            && nexpanded < PP_MAX_BODY)
                            expanded[nexpanded++] =
                                args[va_idx][va_n - 1];
                    } else {
                        if (nexpanded < PP_MAX_BODY)
                            expanded[nexpanded++] = ltk;
                    }
                    i += 3;
                    continue;
                }
            }

            /* normal paste */
            {
                struct tok left_tok;
                struct tok right_tok;
                struct tok pasted;
                int left_param;
                int right_param;
                int left_empty;
                int right_empty;
                left_tok = m->body[i];
                left_param = pp_resolve_param(m, &left_tok);
                left_empty = (left_param >= 0
                    && left_param < narg_lists
                    && nargs[left_param] == 0);
                if (left_param >= 0
                    && left_param < narg_lists
                    && nargs[left_param] > 0) {
                    left_tok = args[left_param]
                                   [nargs[left_param] - 1];
                }
                if (i + 2 < m->body_len) {
                    right_tok = m->body[i + 2];
                    right_param = pp_resolve_param(
                        m, &right_tok);
                    right_empty = (right_param >= 0
                        && right_param < narg_lists
                        && nargs[right_param] == 0);
                    if (right_param >= 0
                        && right_param < narg_lists
                        && nargs[right_param] > 0) {
                        right_tok = args[right_param][0];
                    }
                } else {
                    if (nexpanded < PP_MAX_BODY)
                        expanded[nexpanded++] = left_tok;
                    i += 2;
                    continue;
                }
                /* handle empty macro arguments in paste:
                 * if one side is empty, result is the other side;
                 * if both empty, skip entirely */
                if (left_empty && right_empty) {
                    i += 3;
                    continue;
                }
                if (left_empty) {
                    pasted = right_tok;
                    i += 3;
                } else if (right_empty) {
                    pasted = left_tok;
                    i += 3;
                } else {
                    pasted = paste_tokens(&left_tok,
                                          &right_tok);
                    i += 3;
                }
                /* handle chained paste: A ## B ## C ## D ... */
                while (i < m->body_len
                       && m->body[i].kind == TOK_PASTE
                       && i + 1 < m->body_len) {
                    int chain_empty;
                    right_tok = m->body[i + 1];
                    param_idx = pp_resolve_param(
                        m, &right_tok);
                    chain_empty = (param_idx >= 0
                        && param_idx < narg_lists
                        && nargs[param_idx] == 0);
                    if (param_idx >= 0
                        && param_idx < narg_lists
                        && nargs[param_idx] > 0) {
                        right_tok = args[param_idx][0];
                    }
                    if (chain_empty) {
                        /* empty param: skip paste, keep pasted */
                        i += 2;
                        continue;
                    }
                    pasted = paste_tokens(&pasted, &right_tok);
                    i += 2;
                }
                if (nexpanded < PP_MAX_BODY)
                    expanded[nexpanded++] = pasted;
                continue;
            }
        }

        /* stringify # */
        if (m->is_func && m->body[i].kind == TOK_HASH) {
            if (i + 1 < m->body_len
                && pp_tok_is_name(&m->body[i + 1])) {
                param_idx = -1;
                for (j = 0; j < m->nparams; j++) {
                    if (strcmp(m->body[i + 1].str,
                              m->params[j]) == 0) {
                        param_idx = j;
                        break;
                    }
                }
                if (param_idx < 0 && m->is_variadic
                    && (strcmp(m->body[i + 1].str,
                              "__VA_ARGS__") == 0
                        || (m->va_name != NULL
                            && strcmp(m->body[i + 1].str,
                                     m->va_name) == 0))) {
                    param_idx = m->nparams;
                }
                if (param_idx >= 0) {
                    if (m->name != NULL
                        && strcmp(m->name, "DEFINE") == 0) {
                        fprintf(stderr,
                                "pp DEFINE stringify param=%s arg_n=%d first_kind=%d first_str=%s\n",
                                m->body[i + 1].str,
                                (param_idx < narg_lists)
                                    ? nargs[param_idx] : -1,
                                (param_idx < narg_lists
                                 && nargs[param_idx] > 0)
                                    ? (int)args[param_idx][0].kind : -1,
                                (param_idx < narg_lists
                                 && nargs[param_idx] > 0
                                 && args[param_idx][0].str != NULL)
                                    ? args[param_idx][0].str
                                    : "(null)");
                    }
                    if (param_idx < narg_lists) {
                        stringified = stringify_tokens(
                            args[param_idx],
                            nargs[param_idx]);
                    } else {
                        /* variadic param with no args: stringify to "" */
                        stringified = stringify_tokens(NULL, 0);
                    }
                    if (m->name != NULL
                        && strcmp(m->name, "DEFINE") == 0) {
                        fprintf(stderr,
                                "pp DEFINE stringified result=%s\n",
                                stringified.str != NULL
                                    ? stringified.str : "(null)");
                    }
                    if (nexpanded < PP_MAX_BODY)
                        expanded[nexpanded++] = stringified;
                    i += 2;
                    continue;
                }
            }
        }

        /* parameter substitution (named + __VA_ARGS__) */
        if (m->is_func && pp_tok_is_name(&m->body[i])) {
            param_idx = pp_resolve_param(m, &m->body[i]);
            if (param_idx >= 0) {
                if (param_idx < narg_lists) {
                    struct tok *arg_expanded;
                    int narg_expanded;
                    arg_expanded = pp_expand_pool_get();
                    if (arg_expanded == NULL) {
                        i++;
                        continue;
                    }
                    narg_expanded = 0;
                    pp_expand_tokens(
                        args[param_idx], nargs[param_idx],
                        arg_expanded, &narg_expanded,
                        PP_MAX_BODY);
                    for (j = 0; j < narg_expanded
                         && nexpanded < PP_MAX_BODY; j++)
                        expanded[nexpanded++] =
                            arg_expanded[j];
                    pp_expand_pool_put(arg_expanded);
                }
                i++;
                continue;
            }
        }

        /* normal body token */
        if (nexpanded < PP_MAX_BODY) {
            expanded[nexpanded++] = m->body[i];
        }
        i++;
    }

    /* re-scan expanded body for further macro expansion.
     * Disable the macro during rescan to prevent infinite recursion
     * (C standard "painted blue" rule). */
    m->disabled = 1;
    pp_expand_tokens(expanded, nexpanded, out, out_len, out_max);
    m->disabled = 0;

    pp_expand_pool_put(expanded);
}

/* ---- directive handling ---- */

static void pp_handle_define(void)
{
    struct tok *t;
    struct pp_macro *m;
    const char *p;
    struct tok body_toks[PP_MAX_MACRO_BODY];
    int nbody;
    int i;

    t = pp_raw_next();
    /* Accept keyword tokens as macro names (e.g. #define auto ...) */
    if (!pp_tok_is_name(t)) {
        err_at(t->file, t->line, t->col, "expected macro name after #define");
        pp_skip_line();
        return;
    }

    m = pp_add_macro(t->str);
    m->is_func = 0;
    m->nparams = 0;
    m->body_len = 0;
    m->va_name = NULL;

    /* check for function-like macro: '(' must immediately follow name
     * (no whitespace) */
    p = lex_get_pos();
    if (*p == '(') {
        m->is_func = 1;
        lex_set_pos(p, lex_get_line(), lex_get_col());
        pp_raw_next(); /* consume '(' */

        /* read parameter list */
        t = pp_raw_next();
        if (t->kind != TOK_RPAREN) {
            /* has parameters */
            for (;;) {
                if (t->kind == TOK_ELLIPSIS) {
                    m->is_variadic = 1;
                    t = pp_raw_next(); /* should be ')' */
                    break;
                }
                if (!pp_tok_is_name(t)) {
                    err_at(t->file, t->line, t->col,
                           "expected parameter name in macro definition");
                    pp_skip_line();
                    return;
                }
                if (m->nparams < PP_MAX_PARAMS) {
                    m->params[m->nparams++] = str_dup(pp_arena, t->str, t->len);
                }
                t = pp_raw_next();
                /* GNU named variadic: param... means param is variadic */
                if (t->kind == TOK_ELLIPSIS) {
                    m->is_variadic = 1;
                    m->va_name = m->params[m->nparams - 1];
                    m->nparams--; /* remove from named params */
                    t = pp_raw_next(); /* should be ')' */
                    break;
                }
                if (t->kind == TOK_RPAREN) {
                    break;
                }
                if (t->kind != TOK_COMMA) {
                    err_at(t->file, t->line, t->col,
                           "expected ',' or ')' in macro parameter list");
                    pp_skip_line();
                    return;
                }
                t = pp_raw_next();
            }
        }
    }

    /* read body tokens until end of line */
    nbody = pp_read_line_tokens(body_toks, PP_MAX_MACRO_BODY);

    /* allocate body from arena and copy tokens.
     * The lexer now returns TOK_PASTE for ##, so no detection needed. */
    m->body = (struct tok *)arena_alloc(pp_arena,
        (usize)nbody * sizeof(struct tok));
    m->body_cap = nbody;
    for (i = 0; i < nbody; i++) {
        m->body[i] = body_toks[i];
    }
    m->body_len = nbody;
}

static void pp_handle_undef(void)
{
    struct tok *t;

    t = pp_raw_next();
    /* Accept keyword tokens too (e.g. #undef auto) */
    if (t->str != NULL && t->len > 0) {
        pp_remove_macro(t->str);
    }
    pp_skip_line();
}

static void pp_handle_include_ex(int is_next)
{
    const char *p;
    char name[4096];
    int is_system;
    int len;
    char *content;
    char *saved_filename;
    char resolved_path[4096];
    int found_idx;
    int search_start;
    const char *guard;

    p = lex_get_pos();
    /* skip spaces */
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    is_system = 0;
    len = 0;
    if (*p == '"') {
        /* #include "file" */
        p++;
        while (*p != '"' && *p != '\n' && *p != '\0' && len < (int)sizeof(name) - 1) {
            name[len++] = *p++;
        }
        if (*p == '"') p++;
    } else if (*p == '<') {
        /* #include <file> */
        is_system = 1;
        p++;
        while (*p != '>' && *p != '\n' && *p != '\0' && len < (int)sizeof(name) - 1) {
            name[len++] = *p++;
        }
        if (*p == '>') p++;
    } else {
        err_at(lex_get_filename(), lex_get_line(), lex_get_col(),
               "expected '\"' or '<' after #include");
        pp_skip_line();
        return;
    }
    name[len] = '\0';

    /* advance past rest of line */
    while (*p != '\0' && *p != '\n') {
        p++;
    }
    if (*p == '\n') p++;
    lex_set_pos(p, lex_get_line() + 1, 1);

    /* for include_next, start searching from the NEXT include path */
    search_start = 0;
    if (is_next) {
        search_start = pp_cur_include_path_idx + 1;
    }

    found_idx = -1;
    resolved_path[0] = '\0';
    content = pp_find_include_from(name, is_system || is_next,
                                   search_start, &found_idx,
                                   resolved_path, (int)sizeof(resolved_path));
    if (content == NULL) {
        err_at(lex_get_filename(), lex_get_line(), lex_get_col(),
               "cannot find include file '%s'", name);
        return;
    }

    /* determine filename to use */
    if (resolved_path[0] == '\0') {
        strcpy(resolved_path, name);
    }
    saved_filename = str_dup(pp_arena, resolved_path,
                             (int)strlen(resolved_path));

    if (pp_is_pragma_once(saved_filename)) {
        return;
    }
    if (pp_check_guard_cache(saved_filename)) {
        return;
    }

    /* record dependency for -MD/-MMD */
    pp_record_dep(saved_filename, is_system || is_next);

    /* detect include guard pattern and cache it */
    guard = pp_detect_include_guard(content);
    if (guard != NULL) {
        pp_add_guard_cache(saved_filename, guard);
    }

    pp_push_include_ex(content, saved_filename, found_idx);
}

static void pp_handle_include(void)
{
    pp_handle_include_ex(0);
}

static void pp_handle_include_next(void)
{
    pp_handle_include_ex(1);
}

static void pp_handle_pragma(void)
{
    struct tok *t;

    t = pp_raw_next();
    if (t->kind == TOK_IDENT && strcmp(t->str, "once") == 0) {
        pp_mark_pragma_once(lex_get_filename());
    }
    /* #pragma push_macro("name") */
    else if (t->kind == TOK_IDENT &&
             strcmp(t->str, "push_macro") == 0) {
        struct tok *lp;
        struct tok *nm;
        struct pp_macro *m;
        lp = pp_raw_next(); /* ( */
        nm = pp_raw_next(); /* "name" */
        (void)lp;
        if (nm->kind == TOK_STR && nm->str != NULL &&
            pp_macro_stack_depth < PP_MAX_MACRO_STACK) {
            struct pp_macro_save *sv;
            sv = &pp_macro_stack[pp_macro_stack_depth++];
            strncpy(sv->name, nm->str, sizeof(sv->name) - 1);
            sv->name[sizeof(sv->name) - 1] = '\0';
            m = pp_find_macro(nm->str);
            if (m != NULL) {
                sv->was_defined = 1;
                sv->macro = *m;
            } else {
                sv->was_defined = 0;
                memset(&sv->macro, 0, sizeof(sv->macro));
            }
        }
    }
    /* #pragma pop_macro("name") */
    else if (t->kind == TOK_IDENT &&
             strcmp(t->str, "pop_macro") == 0) {
        struct tok *lp;
        struct tok *nm;
        lp = pp_raw_next(); /* ( */
        nm = pp_raw_next(); /* "name" */
        (void)lp;
        if (nm->kind == TOK_STR && nm->str != NULL &&
            pp_macro_stack_depth > 0) {
            int si;
            /* search stack from top for matching name */
            for (si = pp_macro_stack_depth - 1; si >= 0; si--) {
                if (strcmp(pp_macro_stack[si].name,
                           nm->str) == 0) {
                    struct pp_macro_save *sv;
                    sv = &pp_macro_stack[si];
                    if (sv->was_defined) {
                        struct pp_macro *m;
                        m = pp_find_macro(nm->str);
                        if (m != NULL) {
                            *m = sv->macro;
                        } else {
                            /* re-add macro by creating
                             * a new slot and copying */
                            m = pp_add_macro(nm->str);
                            if (m != NULL) {
                                *m = sv->macro;
                                m->name = str_dup(
                                    pp_arena, nm->str,
                                    (int)strlen(nm->str));
                            }
                        }
                    } else {
                        /* macro wasn't defined; undef it */
                        struct pp_macro *m;
                        m = pp_find_macro(nm->str);
                        if (m != NULL) {
                            m->name = "";
                        }
                    }
                    /* remove from stack */
                    pp_macro_stack_depth = si;
                    break;
                }
            }
        }
    }
    /* skip rest of pragma line */
    pp_skip_line();
}

/* ---- __has_attribute / __has_feature helpers ---- */

/* known attributes for __has_attribute queries */
static const char *pp_known_attrs[] = {
    "noreturn", "unused", "used", "weak", "packed", "deprecated",
    "noinline", "always_inline", "cold", "hot", "pure", "const",
    "malloc", "warn_unused_result", "constructor", "destructor",
    "transparent_union", "aligned", "visibility", "format",
    "section", "alias", "cleanup", "vector_size",
    "nonnull", "sentinel", "nothrow", "leaf",
    "returns_nonnull", "artificial", "gnu_inline", "may_alias",
    "mode", "alloc_size", "access",
    "no_sanitize_address", "no_instrument_function",
    "warn_if_not_aligned", "fallthrough", "error", "warning",
    "externally_visible", "no_reorder", "optimize",
    "target", "flatten", "ifunc", "no_split_stack",
    "copy", "no_stack_protector", "assume_aligned",
    "designated_init", "no_sanitize", "no_sanitize_undefined",
    "no_profile_instrument_function",
    NULL
};

static int pp_has_known_attribute(const char *name)
{
    int i;
    int nlen;
    const char *a;

    /* strip leading/trailing __ if present */
    nlen = (int)strlen(name);
    if (nlen > 4 && name[0] == '_' && name[1] == '_'
        && name[nlen - 1] == '_' && name[nlen - 2] == '_') {
        /* match __attr__ form against bare name */
        for (i = 0; pp_known_attrs[i] != NULL; i++) {
            a = pp_known_attrs[i];
            if ((int)strlen(a) == nlen - 4
                && strncmp(name + 2, a, (size_t)(nlen - 4)) == 0) {
                return 1;
            }
        }
        return 0;
    }

    for (i = 0; pp_known_attrs[i] != NULL; i++) {
        if (strcmp(name, pp_known_attrs[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* known features for __has_feature queries */
static const char *pp_known_features[] = {
    "c_alignas", "c_alignof", "c_atomic",
    "c_generic_selections", "c_static_assert", "c_thread_local",
    NULL
};

static int pp_has_known_feature(const char *name)
{
    int i;

    for (i = 0; pp_known_features[i] != NULL; i++) {
        if (strcmp(name, pp_known_features[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ---- constant expression evaluation for #if ---- */

static long pp_eval_expr(struct tok *toks, int ntoks, int *pos);
static long pp_eval_ternary(struct tok *toks, int ntoks, int *pos);

static long pp_eval_primary(struct tok *toks, int ntoks, int *pos)
{
    long val;
    struct tok *t;

    if (*pos >= ntoks) {
        return 0;
    }
    t = &toks[*pos];

    if (t->kind == TOK_NUM) {
        (*pos)++;
        return t->val;
    }
    if (t->kind == TOK_CHAR_LIT) {
        (*pos)++;
        return t->val;
    }
    if (t->kind == TOK_LPAREN) {
        (*pos)++;
        val = pp_eval_ternary(toks, ntoks, pos);
        if (*pos < ntoks && toks[*pos].kind == TOK_RPAREN) {
            (*pos)++;
        }
        return val;
    }
    if (t->kind == TOK_NOT) {
        (*pos)++;
        return !pp_eval_primary(toks, ntoks, pos);
    }
    if (t->kind == TOK_TILDE) {
        (*pos)++;
        return ~pp_eval_primary(toks, ntoks, pos);
    }
    if (t->kind == TOK_MINUS) {
        (*pos)++;
        return -pp_eval_primary(toks, ntoks, pos);
    }
    if (t->kind == TOK_PLUS) {
        (*pos)++;
        return pp_eval_primary(toks, ntoks, pos);
    }
    if (t->kind == TOK_IDENT) {
        /* "defined" operator */
        if (strcmp(t->str, "defined") == 0) {
            int has_paren;
            (*pos)++;
            has_paren = 0;
            if (*pos < ntoks && toks[*pos].kind == TOK_LPAREN) {
                has_paren = 1;
                (*pos)++;
            }
            if (*pos < ntoks && toks[*pos].str != NULL
                && toks[*pos].len > 0) {
                val = pp_find_macro(toks[*pos].str) != NULL ? 1 : 0;
                (*pos)++;
            } else {
                val = 0;
            }
            if (has_paren && *pos < ntoks && toks[*pos].kind == TOK_RPAREN) {
                (*pos)++;
            }
            return val;
        }
        /* __has_builtin(name) */
        if (strcmp(t->str, "__has_builtin") == 0) {
            (*pos)++;
            if (*pos < ntoks && toks[*pos].kind == TOK_LPAREN) {
                (*pos)++;
            }
            if (*pos < ntoks && toks[*pos].kind == TOK_IDENT) {
                val = builtin_is_known(toks[*pos].str) ? 1 : 0;
                (*pos)++;
            } else {
                val = 0;
            }
            if (*pos < ntoks && toks[*pos].kind == TOK_RPAREN) {
                (*pos)++;
            }
            return val;
        }
        /* __has_attribute(name) */
        if (strcmp(t->str, "__has_attribute") == 0) {
            (*pos)++;
            if (*pos < ntoks && toks[*pos].kind == TOK_LPAREN) {
                (*pos)++;
            }
            if (*pos < ntoks && toks[*pos].kind == TOK_IDENT) {
                val = pp_has_known_attribute(toks[*pos].str) ? 1 : 0;
                (*pos)++;
            } else {
                val = 0;
            }
            if (*pos < ntoks && toks[*pos].kind == TOK_RPAREN) {
                (*pos)++;
            }
            return val;
        }
        /* __has_feature(name) */
        if (strcmp(t->str, "__has_feature") == 0) {
            (*pos)++;
            if (*pos < ntoks && toks[*pos].kind == TOK_LPAREN) {
                (*pos)++;
            }
            if (*pos < ntoks && toks[*pos].kind == TOK_IDENT) {
                val = pp_has_known_feature(toks[*pos].str) ? 1 : 0;
                (*pos)++;
            } else {
                val = 0;
            }
            if (*pos < ntoks && toks[*pos].kind == TOK_RPAREN) {
                (*pos)++;
            }
            return val;
        }
        /* unknown identifier in #if evaluates to 0 */
        (*pos)++;
        return 0;
    }
    (*pos)++;
    return 0;
}

static long pp_eval_unary(struct tok *toks, int ntoks, int *pos)
{
    return pp_eval_primary(toks, ntoks, pos);
}

static long pp_eval_mul(struct tok *toks, int ntoks, int *pos)
{
    long val;
    long rhs;

    val = pp_eval_unary(toks, ntoks, pos);
    while (*pos < ntoks) {
        if (toks[*pos].kind == TOK_STAR) {
            (*pos)++;
            rhs = pp_eval_unary(toks, ntoks, pos);
            val = val * rhs;
        } else if (toks[*pos].kind == TOK_SLASH) {
            (*pos)++;
            rhs = pp_eval_unary(toks, ntoks, pos);
            if (rhs != 0) val = val / rhs;
        } else if (toks[*pos].kind == TOK_PERCENT) {
            (*pos)++;
            rhs = pp_eval_unary(toks, ntoks, pos);
            if (rhs != 0) val = val % rhs;
        } else {
            break;
        }
    }
    return val;
}

static long pp_eval_add(struct tok *toks, int ntoks, int *pos)
{
    long val;

    val = pp_eval_mul(toks, ntoks, pos);
    while (*pos < ntoks) {
        if (toks[*pos].kind == TOK_PLUS) {
            (*pos)++;
            val += pp_eval_mul(toks, ntoks, pos);
        } else if (toks[*pos].kind == TOK_MINUS) {
            (*pos)++;
            val -= pp_eval_mul(toks, ntoks, pos);
        } else {
            break;
        }
    }
    return val;
}

static long pp_eval_shift(struct tok *toks, int ntoks, int *pos)
{
    long val;

    val = pp_eval_add(toks, ntoks, pos);
    while (*pos < ntoks) {
        if (toks[*pos].kind == TOK_LSHIFT) {
            (*pos)++;
            val <<= pp_eval_add(toks, ntoks, pos);
        } else if (toks[*pos].kind == TOK_RSHIFT) {
            (*pos)++;
            val >>= pp_eval_add(toks, ntoks, pos);
        } else {
            break;
        }
    }
    return val;
}

static long pp_eval_rel(struct tok *toks, int ntoks, int *pos)
{
    long val;

    val = pp_eval_shift(toks, ntoks, pos);
    while (*pos < ntoks) {
        if (toks[*pos].kind == TOK_LT) {
            (*pos)++;
            val = val < pp_eval_shift(toks, ntoks, pos);
        } else if (toks[*pos].kind == TOK_GT) {
            (*pos)++;
            val = val > pp_eval_shift(toks, ntoks, pos);
        } else if (toks[*pos].kind == TOK_LE) {
            (*pos)++;
            val = val <= pp_eval_shift(toks, ntoks, pos);
        } else if (toks[*pos].kind == TOK_GE) {
            (*pos)++;
            val = val >= pp_eval_shift(toks, ntoks, pos);
        } else {
            break;
        }
    }
    return val;
}

static long pp_eval_eq(struct tok *toks, int ntoks, int *pos)
{
    long val;

    val = pp_eval_rel(toks, ntoks, pos);
    while (*pos < ntoks) {
        if (toks[*pos].kind == TOK_EQ) {
            (*pos)++;
            val = val == pp_eval_rel(toks, ntoks, pos);
        } else if (toks[*pos].kind == TOK_NE) {
            (*pos)++;
            val = val != pp_eval_rel(toks, ntoks, pos);
        } else {
            break;
        }
    }
    return val;
}

static long pp_eval_bitand(struct tok *toks, int ntoks, int *pos)
{
    long val;

    val = pp_eval_eq(toks, ntoks, pos);
    while (*pos < ntoks && toks[*pos].kind == TOK_AMP) {
        (*pos)++;
        val &= pp_eval_eq(toks, ntoks, pos);
    }
    return val;
}

static long pp_eval_bitxor(struct tok *toks, int ntoks, int *pos)
{
    long val;

    val = pp_eval_bitand(toks, ntoks, pos);
    while (*pos < ntoks && toks[*pos].kind == TOK_CARET) {
        (*pos)++;
        val ^= pp_eval_bitand(toks, ntoks, pos);
    }
    return val;
}

static long pp_eval_bitor(struct tok *toks, int ntoks, int *pos)
{
    long val;

    val = pp_eval_bitxor(toks, ntoks, pos);
    while (*pos < ntoks && toks[*pos].kind == TOK_PIPE) {
        (*pos)++;
        val |= pp_eval_bitxor(toks, ntoks, pos);
    }
    return val;
}

static long pp_eval_logand(struct tok *toks, int ntoks, int *pos)
{
    long val;

    val = pp_eval_bitor(toks, ntoks, pos);
    while (*pos < ntoks && toks[*pos].kind == TOK_AND) {
        (*pos)++;
        val = pp_eval_bitor(toks, ntoks, pos) && val;
    }
    return val;
}

static long pp_eval_logor(struct tok *toks, int ntoks, int *pos)
{
    long val;

    val = pp_eval_logand(toks, ntoks, pos);
    while (*pos < ntoks && toks[*pos].kind == TOK_OR) {
        (*pos)++;
        val = pp_eval_logand(toks, ntoks, pos) || val;
    }
    return val;
}

static long pp_eval_ternary(struct tok *toks, int ntoks, int *pos)
{
    long cond;
    long then_val;
    long else_val;

    cond = pp_eval_logor(toks, ntoks, pos);
    if (*pos < ntoks && toks[*pos].kind == TOK_QUESTION) {
        (*pos)++;
        then_val = pp_eval_ternary(toks, ntoks, pos);
        if (*pos < ntoks && toks[*pos].kind == TOK_COLON) {
            (*pos)++;
        }
        else_val = pp_eval_ternary(toks, ntoks, pos);
        return cond ? then_val : else_val;
    }
    return cond;
}

static long pp_eval_expr(struct tok *toks, int ntoks, int *pos)
{
    return pp_eval_ternary(toks, ntoks, pos);
}

static long pp_eval_if_expr(void)
{
    struct tok toks[PP_MAX_MACRO_BODY];
    struct tok *expanded;
    int ntoks;
    int nexpanded;
    int pos;
    long result;

    ntoks = pp_read_line_tokens(toks, PP_MAX_MACRO_BODY);

    /* expand macros in the expression (except 'defined') */
    expanded = pp_expand_pool_get();
    if (expanded == NULL) {
        return 0;
    }
    nexpanded = 0;
    pp_expand_tokens(toks, ntoks, expanded, &nexpanded, PP_MAX_BODY);

    pos = 0;
    result = pp_eval_expr(expanded, nexpanded, &pos);
    pp_expand_pool_put(expanded);
    return result;
}

/* ---- conditional directive handling ---- */

static void pp_handle_ifdef(int negate)
{
    struct tok *t;
    struct pp_cond *c;
    int macro_exists;

    t = pp_raw_next();
    /* Accept keyword tokens too (e.g. #ifdef auto) */
    if (t->str == NULL || t->len == 0) {
        err_at(t->file, t->line, t->col, "expected identifier after #ifdef/#ifndef");
        pp_skip_line();
        return;
    }

    macro_exists = pp_find_macro(t->str) != NULL;
    if (negate) {
        macro_exists = !macro_exists;
    }

    pp_skip_line();

    if (pp_cond_depth >= PP_MAX_COND) {
        err("conditional nesting too deep");
        return;
    }

    {
        /* check parent active state BEFORE pushing new entry */
        int parent_active = pp_cond_active();
        c = &pp_cond_stack[pp_cond_depth++];
        c->active = parent_active ? macro_exists : 0;
        /* if parent inactive, prevent #else from activating */
        c->has_been_true = parent_active ? c->active : 1;
        c->is_else = 0;
    }
}

static void pp_handle_if(void)
{
    struct pp_cond *c;
    long val;

    if (!pp_cond_active()) {
        /* skip the expression */
        pp_skip_line();
        if (pp_cond_depth >= PP_MAX_COND) {
            err("conditional nesting too deep");
            return;
        }
        c = &pp_cond_stack[pp_cond_depth++];
        c->active = 0;
        c->has_been_true = 1; /* prevent #else from activating */
        c->is_else = 0;
        return;
    }

    val = pp_eval_if_expr();

    if (pp_cond_depth >= PP_MAX_COND) {
        err("conditional nesting too deep");
        return;
    }
    c = &pp_cond_stack[pp_cond_depth++];
    c->active = val != 0;
    c->has_been_true = c->active;
    c->is_else = 0;
}

static void pp_handle_elif(void)
{
    struct pp_cond *c;
    long val;

    if (pp_cond_depth == 0) {
        err_at(lex_get_filename(), lex_get_line(), lex_get_col(),
               "#elif without matching #if");
        pp_skip_line();
        return;
    }
    c = &pp_cond_stack[pp_cond_depth - 1];

    if (c->has_been_true) {
        c->active = 0;
        pp_skip_line();
        return;
    }

    val = pp_eval_if_expr();
    c->active = val != 0;
    if (c->active) {
        c->has_been_true = 1;
    }
}

static void pp_handle_else(void)
{
    struct pp_cond *c;

    pp_skip_line();

    if (pp_cond_depth == 0) {
        err_at(lex_get_filename(), lex_get_line(), lex_get_col(),
               "#else without matching #if");
        return;
    }
    c = &pp_cond_stack[pp_cond_depth - 1];
    if (c->is_else) {
        err_at(lex_get_filename(), lex_get_line(), lex_get_col(),
               "duplicate #else");
        return;
    }
    c->is_else = 1;
    c->active = !c->has_been_true;
    if (c->active) {
        c->has_been_true = 1;
    }
}

static void pp_handle_endif(void)
{
    pp_skip_line();

    if (pp_cond_depth == 0) {
        err_at(lex_get_filename(), lex_get_line(), lex_get_col(),
               "#endif without matching #if");
        return;
    }
    pp_cond_depth--;
}

/* ---- handle a # directive ---- */

static int pp_handle_directive(void)
{
    struct tok *t;
    const char *dir;

    /* we've seen '#' at beginning of logical line */
    t = pp_raw_next();

    if (t->kind == TOK_EOF) {
        return 0;
    }

    /* empty directive "#\n" is valid.
     * Directive names like "else" may be lexed as keywords (TOK_ELSE)
     * rather than TOK_IDENT, so accept any token with a .str field. */
    if (t->str == NULL) {
        if (t->kind == TOK_NUM) {
            /* GNU linemarker: # linenum ["filename" [flags]]
             * Treat like #line directive. */
            int lnum;
            lnum = (int)t->val;
            t = pp_raw_next();
            if (t->kind == TOK_STR && t->str != NULL) {
                lex_set_filename(str_dup(pp_arena, t->str, t->len));
            }
            pp_skip_line();
            lex_set_pos(lex_get_pos(), lnum, lex_get_col());
            return 1;
        }
        /* empty directive "#\n" is valid */
        pp_skip_line();
        return 1;
    }

    dir = t->str;

    /* conditional directives are always processed regardless of active state */
    if (strcmp(dir, "ifdef") == 0) {
        pp_handle_ifdef(0);
        return 1;
    }
    if (strcmp(dir, "ifndef") == 0) {
        pp_handle_ifdef(1);
        return 1;
    }
    if (strcmp(dir, "if") == 0) {
        pp_handle_if();
        return 1;
    }
    if (strcmp(dir, "elif") == 0) {
        pp_handle_elif();
        return 1;
    }
    if (strcmp(dir, "else") == 0) {
        pp_handle_else();
        return 1;
    }
    if (strcmp(dir, "endif") == 0) {
        pp_handle_endif();
        return 1;
    }

    /* remaining directives only execute if active */
    if (!pp_cond_active()) {
        pp_skip_line();
        return 1;
    }

    if (strcmp(dir, "define") == 0) {
        pp_handle_define();
        return 1;
    }
    if (strcmp(dir, "undef") == 0) {
        pp_handle_undef();
        return 1;
    }
    if (strcmp(dir, "include") == 0) {
        pp_handle_include();
        return 1;
    }
    if (strcmp(dir, "include_next") == 0) {
        pp_handle_include_next();
        return 1;
    }
    if (strcmp(dir, "pragma") == 0) {
        pp_handle_pragma();
        return 1;
    }
    if (strcmp(dir, "error") == 0) {
        char *msg = pp_read_line_text();
        err_at(t->file, t->line, t->col, "#error %s", msg);
        return 1;
    }
    if (strcmp(dir, "warning") == 0) {
        /* just skip */
        pp_skip_line();
        return 1;
    }
    if (strcmp(dir, "line") == 0) {
        /* #line directive: macro-expand args, set line/file */
        {
            struct tok raw[PP_MAX_MACRO_BODY];
            struct tok expanded_line[PP_MAX_MACRO_BODY];
            int nraw;
            int nexp;
            nraw = pp_read_line_tokens(raw, PP_MAX_MACRO_BODY);
            nexp = 0;
            pp_expand_tokens(raw, nraw, expanded_line,
                             &nexp, PP_MAX_MACRO_BODY);
            if (nexp > 0 && expanded_line[0].kind == TOK_NUM) {
                /* set line number (the NEXT line will be this) */
                lex_set_pos(lex_get_pos(),
                            (int)expanded_line[0].val,
                            lex_get_col());
                if (nexp > 1
                    && expanded_line[1].kind == TOK_STR
                    && expanded_line[1].str != NULL) {
                    lex_set_filename(str_dup(
                        pp_arena, expanded_line[1].str,
                        expanded_line[1].len));
                }
            }
        }
        return 1;
    }

    /* unknown directive - skip */
    pp_skip_line();
    return 1;
}

/* ---- public interface ---- */

void pp_init(const char *src, const char *filename, struct arena *a)
{
    int base_std;
    long stdc_ver;
    time_t now;
    struct tm *tm_now;
    char date_buf[16];
    char time_buf[16];
    static const char *mon_names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    pp_arena = a;
    pp_nmacros = 0;
    pp_inc_depth = 0;
    pp_cond_depth = 0;
    pp_tokbuf_len = 0;
    pp_tokbuf_pos = 0;
    pp_at_bol = 1;
    pp_dep_reset();

    /* initialize hash tables */
    memset(pp_hash_buckets, -1, sizeof(pp_hash_buckets));
    memset(pp_macro_next, -1, sizeof(pp_macro_next));
    memset(pp_once_buckets, 0, sizeof(pp_once_buckets));
    pp_nonce_pool = 0;
    memset(pp_guard_cache, 0, sizeof(pp_guard_cache));
    pp_nguards = 0;
    pp_expand_pool_top = 0;

    lex_init(src, filename, a);

    /* record main input file as first dependency */
    pp_record_dep(filename, 0);

    /* ---- GCC compatibility ---- */
    pp_define_num("__GNUC__", 13);
    pp_define_num("__GNUC_MINOR__", 3);
    pp_define_num("__GNUC_PATCHLEVEL__", 0);

    /* ---- free-cc identification ---- */
    pp_define_num("__FREE_CC__", 1);
    pp_define_num("__FREE_CC_MAJOR__", 0);
    pp_define_num("__FREE_CC_MINOR__", 1);

    /* ---- target architecture ---- */
    if (cc_target_arch == TARGET_AARCH64) {
        pp_define_num("__aarch64__", 1);
        pp_define_num("__ARM_64BIT_STATE", 1);
        pp_define_num("__ARM_ARCH", 8);
        pp_define_num("__AARCH64EL__", 1);
    } else if (cc_target_arch == TARGET_X86_64) {
        pp_define_num("__x86_64__", 1);
        pp_define_num("__x86_64", 1);
        pp_define_num("__amd64__", 1);
    }

    /* ---- OS: Linux / Unix ---- */
    pp_define_num("__linux__", 1);
    pp_define_num("__linux", 1);
    pp_define_num("linux", 1);
    pp_define_num("__unix__", 1);
    pp_define_num("__unix", 1);
    pp_define_num("unix", 1);
    pp_define_num("__gnu_linux__", 1);
    pp_define_num("__ELF__", 1);

    /* ---- data model: LP64 and sizeof constants ---- */
    pp_define_num("__LP64__", 1);
    pp_define_num("_LP64", 1);
    pp_define_num("__SIZEOF_CHAR__", 1);
    pp_define_num("__SIZEOF_SHORT__", 2);
    pp_define_num("__SIZEOF_INT__", 4);
    pp_define_num("__SIZEOF_LONG__", 8);
    pp_define_num("__SIZEOF_POINTER__", 8);
    pp_define_num("__SIZEOF_LONG_LONG__", 8);
    pp_define_num("__SIZEOF_FLOAT__", 4);
    pp_define_num("__SIZEOF_DOUBLE__", 8);
    pp_define_num("__SIZEOF_SIZE_T__", 8);
    pp_define_num("__SIZEOF_PTRDIFF_T__", 8);
    pp_define_num("__SIZEOF_WCHAR_T__", 4);
    pp_define_num("__SIZEOF_WINT_T__", 4);
    pp_define_num("__SIZEOF_INT128__", 16);
    pp_define_num("__SIZEOF_LONG_DOUBLE__", 16);

    /* ---- type limits ---- */
    pp_define_num("__CHAR_BIT__", 8);
    pp_define_num("__SCHAR_MAX__", 127);
    pp_define_num("__SHRT_MAX__", 32767);
    pp_define_num("__INT_MAX__", 2147483647L);
    pp_define_num("__LONG_MAX__", 9223372036854775807L);
    pp_define_num("__LONG_LONG_MAX__", 9223372036854775807L);

    /* ---- floating-point characteristics (IEEE 754) ---- */
    pp_define_num("__FLT_RADIX__", 2);
    pp_define_num("__FLT_MANT_DIG__", 24);
    pp_define_num("__FLT_DIG__", 6);
    pp_define_num("__FLT_MIN_EXP__", -125);
    pp_define_num("__FLT_MAX_EXP__", 128);
    pp_define_num("__FLT_HAS_INFINITY__", 1);
    pp_define_num("__FLT_HAS_QUIET_NAN__", 1);
    pp_define_num("__DBL_MANT_DIG__", 53);
    pp_define_num("__DBL_DIG__", 15);
    pp_define_num("__DBL_MIN_EXP__", -1021);
    pp_define_num("__DBL_MAX_EXP__", 1024);
    pp_define_num("__DBL_HAS_INFINITY__", 1);
    pp_define_num("__DBL_HAS_QUIET_NAN__", 1);
    pp_define_num("__LDBL_MANT_DIG__", 113);
    pp_define_num("__LDBL_DIG__", 33);
    pp_define_num("__LDBL_MIN_EXP__", -16381);
    pp_define_num("__LDBL_MAX_EXP__", 16384);
    pp_define_num("__FLT_EVAL_METHOD__", 0);
    pp_define_num("__DECIMAL_DIG__", 36);

    /* ---- float/double/long double limit constants ---- */
    pp_define_float("__FLT_MIN__", 1.17549435e-38);
    pp_define_float("__FLT_MAX__", 3.40282347e+38);
    pp_define_float("__FLT_EPSILON__", 1.19209290e-7);
    pp_define_float("__FLT_DENORM_MIN__", 1.40129846e-45);
    pp_define_float("__DBL_MIN__", 2.2250738585072014e-308);
    pp_define_float("__DBL_MAX__", 1.7976931348623157e+308);
    pp_define_float("__DBL_EPSILON__", 2.2204460492503131e-16);
    pp_define_float("__DBL_DENORM_MIN__", 5.0e-324);
    /* long double limits: use double approximation since host may not
     * support aarch64 128-bit long double */
    pp_define_float("__LDBL_MIN__", 2.2250738585072014e-308);
    pp_define_float("__LDBL_MAX__", 1.7976931348623157e+308);
    pp_define_float("__LDBL_EPSILON__", 1.0842021724855044e-19);

    /* ---- GCC atomic memory order constants ---- */
    pp_define_num("__ATOMIC_RELAXED", 0);
    pp_define_num("__ATOMIC_CONSUME", 1);
    pp_define_num("__ATOMIC_ACQUIRE", 2);
    pp_define_num("__ATOMIC_RELEASE", 3);
    pp_define_num("__ATOMIC_ACQ_REL", 4);
    pp_define_num("__ATOMIC_SEQ_CST", 5);

    /* ---- GCC builtin type macros (LP64 aarch64) ---- */
    pp_define_kw2("__SIZE_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_LONG, "long");
    pp_define_kw2("__PTRDIFF_TYPE__", TOK_LONG, "long", TOK_INT, "int");
    pp_define_kw2("__WCHAR_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_INT, "int");
    pp_define_kw2("__WINT_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_INT, "int");
    pp_define_kw2("__INTPTR_TYPE__", TOK_LONG, "long", TOK_INT, "int");
    pp_define_kw2("__UINTPTR_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_LONG, "long");
    pp_define_kw2("__INT64_TYPE__", TOK_LONG, "long", TOK_INT, "int");
    pp_define_kw2("__UINT64_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_LONG, "long");
    pp_define_kw1("__INT32_TYPE__", TOK_INT, "int");
    pp_define_kw2("__UINT32_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_INT, "int");
    pp_define_kw2("__INT16_TYPE__", TOK_SHORT, "short", TOK_INT, "int");
    pp_define_kw2("__UINT16_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_SHORT, "short");
    pp_define_kw2("__INT8_TYPE__",
                  TOK_SIGNED, "signed", TOK_CHAR_KW, "char");
    pp_define_kw2("__UINT8_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_CHAR_KW, "char");
    pp_define_kw2("__INT_LEAST8_TYPE__",
                  TOK_SIGNED, "signed", TOK_CHAR_KW, "char");
    pp_define_kw2("__UINT_LEAST8_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_CHAR_KW, "char");
    pp_define_kw1("__INT_LEAST16_TYPE__", TOK_SHORT, "short");
    pp_define_kw2("__UINT_LEAST16_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_SHORT, "short");
    pp_define_kw1("__INT_LEAST32_TYPE__", TOK_INT, "int");
    pp_define_kw2("__UINT_LEAST32_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_INT, "int");
    pp_define_kw2("__INT_LEAST64_TYPE__",
                  TOK_LONG, "long", TOK_INT, "int");
    pp_define_kw2("__UINT_LEAST64_TYPE__",
                  TOK_UNSIGNED, "unsigned", TOK_LONG, "long");

    /* ---- byte order ---- */
    pp_define_num("__ORDER_LITTLE_ENDIAN__", 1234);
    pp_define_num("__ORDER_BIG_ENDIAN__", 4321);
    pp_define_ident("__BYTE_ORDER__", "__ORDER_LITTLE_ENDIAN__");

    /* ---- C standard conformance ---- */
    pp_define_num("__STDC__", 1);

    /* determine base standard level for __STDC_VERSION__ */
    base_std = cc_std.std_level;
    if (base_std >= STD_GNU89) {
        base_std = base_std - STD_GNU89;
    }
    stdc_ver = 0;
    if (base_std == STD_C89) {
        stdc_ver = 199409L;
    } else if (base_std == STD_C99) {
        stdc_ver = 199901L;
    } else if (base_std == STD_C11) {
        stdc_ver = 201112L;
    } else if (base_std == STD_C23) {
        stdc_ver = 202311L;
    }
    if (stdc_ver != 0) {
        pp_define_num("__STDC_VERSION__", stdc_ver);
    }

    /* ---- hosted/freestanding ---- */
    if (cc_freestanding) {
        pp_define_num("__STDC_HOSTED__", 0);
    } else {
        pp_define_num("__STDC_HOSTED__", 1);
    }

    /* ---- __DATE__ and __TIME__ ---- */
    now = time(NULL);
    tm_now = localtime(&now);
    if (tm_now != NULL) {
        sprintf(date_buf, "%s %2d %04d",
                mon_names[tm_now->tm_mon],
                tm_now->tm_mday,
                tm_now->tm_year + 1900);
        sprintf(time_buf, "%02d:%02d:%02d",
                tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
    } else {
        strcpy(date_buf, "Jan  1 1970");
        strcpy(time_buf, "00:00:00");
    }
    pp_define_str("__DATE__", date_buf);
    pp_define_str("__TIME__", time_buf);

    /* ---- apply command-line -D definitions ---- */
    {
        int ci;
        for (ci = 0; ci < pp_ncmd_defines; ci++) {
            pp_define_cmdline(pp_cmd_defines[ci]);
        }
    }

    /* ---- apply command-line -U undefinitions ---- */
    {
        int ci;
        for (ci = 0; ci < pp_ncmd_undefs; ci++) {
            pp_undef_cmdline(pp_cmd_undefs[ci]);
        }
    }

    /* ---- process -include force-includes ---- */
    {
        int ci;
        for (ci = 0; ci < pp_ncmd_force_inc; ci++) {
            const char *fpath;
            FILE *finc;
            long fsz;
            char *fsrc;
            size_t fnread;

            fpath = pp_cmd_force_inc[ci];
            finc = fopen(fpath, "r");
            if (finc == NULL) {
                fprintf(stderr,
                    "free-cc: cannot open -include file '%s'\n",
                    fpath);
                continue;
            }
            fseek(finc, 0, SEEK_END);
            fsz = ftell(finc);
            fseek(finc, 0, SEEK_SET);
            fsrc = (char *)arena_alloc(pp_arena,
                                       (usize)(fsz + 2));
            fnread = fread(fsrc, 1, (size_t)fsz, finc);
            fclose(finc);
            if (fnread > 0 && fsrc[fnread - 1] != '\n') {
                fsrc[fnread] = '\n';
                fnread++;
            }
            fsrc[fnread] = '\0';
            pp_push_include(fsrc, fpath);
        }
    }
}

struct tok *pp_next(void)
{
    struct tok *t;
    struct pp_macro *m;
    struct tok *result;
    const char *fn;
    struct tok *peek_tok;
    struct tok *arg_toks;
    struct tok *args[PP_MAX_PARAMS];
    int nargs[PP_MAX_PARAMS];
    int narg_lists;
    int depth;
    int arg_start;
    int n;
    struct tok *at;

    for (;;) {

    /* return buffered expanded tokens first */
    if (pp_tokbuf_pos < pp_tokbuf_len) {
        struct tok buf_tok;
        buf_tok = pp_tokbuf[pp_tokbuf_pos];

        /* Check if this buffered token is a function-like macro name
         * followed by '(' -- either next in the buffer or from the
         * lexer stream.  This handles the case where an object-like
         * macro expands to a function-like macro name, e.g.:
         *   #define api_check luai_apicheck
         *   api_check(L, 1)
         * After expanding api_check, pp_tokbuf = [luai_apicheck].
         * We must check if '(' follows so we can expand it. */
        if (pp_tok_is_name(&buf_tok)) {
            struct tok *next_tok;
            int next_is_lparen;
            next_is_lparen = 0;

            if (pp_tokbuf_pos + 1 < pp_tokbuf_len) {
                next_is_lparen =
                    (pp_tokbuf[pp_tokbuf_pos + 1].kind
                     == TOK_LPAREN);
            } else {
                next_tok = lex_peek();
                next_is_lparen =
                    (next_tok->kind == TOK_LPAREN);
            }

            if (next_is_lparen) {
                m = pp_find_macro(buf_tok.str);
                if (m != NULL && !m->disabled && !buf_tok.no_expand
                    && m->is_func) {
                    /* consume the macro name from buffer */
                    pp_tokbuf_pos++;
                    if (pp_tokbuf_pos >= pp_tokbuf_len) {
                        pp_tokbuf_pos = 0;
                        pp_tokbuf_len = 0;
                    }

                    /* consume '(' -- from buffer or lexer */
                    if (pp_tokbuf_pos < pp_tokbuf_len
                        && pp_tokbuf[pp_tokbuf_pos].kind
                           == TOK_LPAREN) {
                        pp_tokbuf_pos++;
                        if (pp_tokbuf_pos >= pp_tokbuf_len) {
                            pp_tokbuf_pos = 0;
                            pp_tokbuf_len = 0;
                        }
                    } else {
                        lex_next(); /* consume '(' from lexer */
                    }

                    /* collect arguments from remaining buffer
                     * and/or lexer */
                    {
                        struct tok *fa_toks;
                        int fa_n;
                        int fa_narg_lists;
                        int fa_depth;
                        int fa_arg_start;

                        fa_toks = (struct tok *)malloc(
                            (usize)PP_MAX_BODY
                            * sizeof(struct tok));
                        if (fa_toks == NULL) {
                            continue;
                        }
                        fa_n = 0;
                        fa_narg_lists = 0;
                        fa_depth = 1;
                        fa_arg_start = 0;

                        /* drain remaining buffer tokens first */
                        while (pp_tokbuf_pos < pp_tokbuf_len
                               && fa_depth > 0) {
                            at = &pp_tokbuf[pp_tokbuf_pos++];
                            if (at->kind == TOK_LPAREN) {
                                fa_depth++;
                                if (fa_n < PP_MAX_BODY)
                                    fa_toks[fa_n++] = *at;
                            } else if (at->kind == TOK_RPAREN) {
                                fa_depth--;
                                if (fa_depth == 0) {
                                    if (fa_n > fa_arg_start
                                        || fa_narg_lists > 0) {
                                        args[fa_narg_lists] =
                                            &fa_toks[fa_arg_start];
                                        nargs[fa_narg_lists] =
                                            fa_n - fa_arg_start;
                                        fa_narg_lists++;
                                    }
                                } else {
                                    if (fa_n < PP_MAX_BODY)
                                        fa_toks[fa_n++] = *at;
                                }
                            } else if (at->kind == TOK_COMMA
                                       && fa_depth == 1
                                       && !(m->is_variadic
                                            && fa_narg_lists
                                               >= m->nparams)) {
                                args[fa_narg_lists] =
                                    &fa_toks[fa_arg_start];
                                nargs[fa_narg_lists] =
                                    fa_n - fa_arg_start;
                                fa_narg_lists++;
                                fa_arg_start = fa_n;
                            } else {
                                if (fa_n < PP_MAX_BODY)
                                    fa_toks[fa_n++] = *at;
                            }
                        }
                        if (pp_tokbuf_pos >= pp_tokbuf_len) {
                            pp_tokbuf_pos = 0;
                            pp_tokbuf_len = 0;
                        }

                        /* continue from lexer if needed */
                        while (fa_depth > 0) {
                            at = lex_next();
                            if (at->kind == TOK_EOF) break;
                            if (at->kind == TOK_LPAREN) {
                                fa_depth++;
                                if (fa_n < PP_MAX_BODY)
                                    fa_toks[fa_n++] = *at;
                            } else if (at->kind == TOK_RPAREN) {
                                fa_depth--;
                                if (fa_depth == 0) {
                                    if (fa_n > fa_arg_start
                                        || fa_narg_lists > 0) {
                                        args[fa_narg_lists] =
                                            &fa_toks[fa_arg_start];
                                        nargs[fa_narg_lists] =
                                            fa_n - fa_arg_start;
                                        fa_narg_lists++;
                                    }
                                } else {
                                    if (fa_n < PP_MAX_BODY)
                                        fa_toks[fa_n++] = *at;
                                }
                            } else if (at->kind == TOK_COMMA
                                       && fa_depth == 1
                                       && !(m->is_variadic
                                            && fa_narg_lists
                                               >= m->nparams)) {
                                args[fa_narg_lists] =
                                    &fa_toks[fa_arg_start];
                                nargs[fa_narg_lists] =
                                    fa_n - fa_arg_start;
                                fa_narg_lists++;
                                fa_arg_start = fa_n;
                            } else {
                                if (fa_n < PP_MAX_BODY)
                                    fa_toks[fa_n++] = *at;
                            }
                        }

                        /* Save any remaining tokens in
                         * pp_tokbuf that follow the macro
                         * call's closing ')'.  We must
                         * preserve them because the expand
                         * below will overwrite pp_tokbuf. */
                        {
                            struct tok *saved;
                            int nsaved;
                            int si;
                            nsaved = pp_tokbuf_len
                                     - pp_tokbuf_pos;
                            saved = NULL;
                            if (nsaved > 0) {
                                saved = (struct tok *)malloc(
                                    (usize)nsaved
                                    * sizeof(struct tok));
                                if (saved != NULL) {
                                    for (si = 0; si < nsaved;
                                         si++)
                                        saved[si] =
                                            pp_tokbuf[
                                            pp_tokbuf_pos
                                            + si];
                                }
                            }

                            pp_tokbuf_len = 0;
                            pp_tokbuf_pos = 0;
                            pp_expand_macro(
                                m, args, nargs,
                                fa_narg_lists,
                                pp_tokbuf, &pp_tokbuf_len,
                                PP_MAX_EXPAND);
                            free(fa_toks);
                            fa_toks = NULL;

                            /* Append saved remaining tokens
                             * after the expansion result */
                            if (saved != NULL && nsaved > 0) {
                                for (si = 0; si < nsaved
                                     && pp_tokbuf_len
                                        < PP_MAX_EXPAND;
                                     si++)
                                    pp_tokbuf[
                                        pp_tokbuf_len++] =
                                        saved[si];
                                free(saved);
                            }
                        }

                        if (pp_tokbuf_len > 0) {
                            result = (struct tok *)arena_alloc(
                                pp_arena, sizeof(struct tok));
                            *result =
                                pp_tokbuf[pp_tokbuf_pos++];
                            if (pp_tokbuf_pos
                                >= pp_tokbuf_len) {
                                pp_tokbuf_pos = 0;
                                pp_tokbuf_len = 0;
                            }
                            return result;
                        }
                        continue;
                    }
                }
            }
        }

        pp_tokbuf_pos++;
        if (pp_tokbuf_pos >= pp_tokbuf_len) {
            pp_tokbuf_pos = 0;
            pp_tokbuf_len = 0;
        }
        result = (struct tok *)arena_alloc(pp_arena, sizeof(struct tok));
        *result = buf_tok;
        return result;
    }
        t = lex_peek();

        /* handle end of included file */
        if (t->kind == TOK_EOF) {
            if (pp_pop_include()) {
                continue;
            }
            /* real EOF */
            result = (struct tok *)arena_alloc(pp_arena, sizeof(struct tok));
            *result = *t;
            return result;
        }

        /* check for # at beginning of line (preprocessor directive) */
        if (t->kind == TOK_HASH) {
            lex_next(); /* consume # */
            pp_handle_directive();
            continue;
        }

        /* if in inactive conditional block, skip tokens */
        if (!pp_cond_active()) {
            lex_next();
            continue;
        }

        t = lex_next();

        /* handle predefined macros */
        if (pp_tok_is_name(t)) {
            if (strcmp(t->str, "__FILE__") == 0) {
                fn = lex_get_filename();
                result = (struct tok *)arena_alloc(pp_arena, sizeof(struct tok));
                memset(result, 0, sizeof(struct tok));
                result->kind = TOK_STR;
                result->str = str_dup(pp_arena, fn, (int)strlen(fn));
                result->len = (int)strlen(fn);
                result->raw = NULL;
                result->file = fn;
                result->line = t->line;
                result->col = t->col;
                return result;
            }
            if (strcmp(t->str, "__LINE__") == 0) {
                result = (struct tok *)arena_alloc(pp_arena, sizeof(struct tok));
                memset(result, 0, sizeof(struct tok));
                result->kind = TOK_NUM;
                result->val = t->line;
                result->raw = NULL;
                result->file = t->file;
                result->line = t->line;
                result->col = t->col;
                return result;
            }

            /* check for macro expansion */
            m = pp_find_macro(t->str);
            if (m != NULL && !m->disabled && !t->no_expand) {
                if (m->is_func) {
                    peek_tok = lex_peek();
                    if (peek_tok->kind == TOK_LPAREN) {
                        lex_next(); /* consume '(' */
                        arg_toks = (struct tok *)malloc(
                            (usize)PP_MAX_BODY * sizeof(struct tok));
                        if (arg_toks == NULL) {
                            continue;
                        }
                        n = 0;
                        narg_lists = 0;
                        depth = 1;
                        arg_start = 0;

                        while (depth > 0) {
                            at = lex_next();
                            if (at->kind == TOK_EOF) {
                                err_at(at->file, at->line, at->col,
                                       "unexpected EOF in macro arguments");
                                break;
                            }
                            if (at->kind == TOK_LPAREN) {
                                depth++;
                                if (n < PP_MAX_BODY) arg_toks[n++] = *at;
                            } else if (at->kind == TOK_RPAREN) {
                                depth--;
                                if (depth == 0) {
                                    if (n > arg_start || narg_lists > 0) {
                                        args[narg_lists] = &arg_toks[arg_start];
                                        nargs[narg_lists] = n - arg_start;
                                        narg_lists++;
                                    }
                                } else {
                                    if (n < PP_MAX_BODY) arg_toks[n++] = *at;
                                }
                            } else if (at->kind == TOK_COMMA && depth == 1
                                       && !(m->is_variadic && narg_lists >= m->nparams)) {
                                args[narg_lists] = &arg_toks[arg_start];
                                nargs[narg_lists] = n - arg_start;
                                narg_lists++;
                                arg_start = n;
                            } else {
                                if (n < PP_MAX_BODY) arg_toks[n++] = *at;
                            }
                        }

                        pp_tokbuf_len = 0;
                        pp_tokbuf_pos = 0;
                        pp_expand_macro(m, args, nargs, narg_lists,
                                        pp_tokbuf, &pp_tokbuf_len, PP_MAX_EXPAND);
                        free(arg_toks);
                        arg_toks = NULL;

                        /* Loop back to buffer check so that if
                         * the expansion produces a function-like
                         * macro name followed by '(', it gets
                         * expanded (e.g. CAT(A,B)(x) where AB is
                         * a function-like macro). */
                        continue;
                    }
                    /* no '(' follows, return as regular identifier */
                } else {
                    /* object-like macro -- expand and loop back
                     * to the buffer check so that if the expansion
                     * is a function-like macro name followed by '(',
                     * it gets expanded properly. */
                    pp_tokbuf_len = 0;
                    pp_tokbuf_pos = 0;
                    pp_expand_macro(m, NULL, NULL, 0,
                                    pp_tokbuf, &pp_tokbuf_len, PP_MAX_EXPAND);
                    continue;
                }
            }
        }

        /* keyword tokens that have been #define'd (e.g. #define auto ...) */
        if (t->kind >= TOK_AUTO && t->kind <= TOK_COMPLEX
            && t->str != NULL && t->len > 0) {
            m = pp_find_macro(t->str);
            if (m != NULL && !m->disabled && !t->no_expand) {
                if (!m->is_func) {
                    /* object-like macro on keyword -- expand and
                     * loop back to the buffer check. */
                    pp_tokbuf_len = 0;
                    pp_tokbuf_pos = 0;
                    pp_expand_macro(m, NULL, NULL, 0,
                                    pp_tokbuf, &pp_tokbuf_len,
                                    PP_MAX_EXPAND);
                    continue;
                }
            }
        }

        /* return the token as-is */
        result = (struct tok *)arena_alloc(pp_arena, sizeof(struct tok));
        *result = *t;
        return result;
    }
}

/* ---- preprocess-only output (-E) ---- */

static void pp_write_escaped_str(FILE *out, const char *s)
{
    unsigned char c;

    while (*s) {
        c = (unsigned char)*s++;
        switch (c) {
        case '\\':
            fputs("\\\\", out);
            break;
        case '"':
            fputs("\\\"", out);
            break;
        case '\a':
            fputs("\\a", out);
            break;
        case '\b':
            fputs("\\b", out);
            break;
        case '\f':
            fputs("\\f", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        case '\v':
            fputs("\\v", out);
            break;
        default:
            if (c < 32 || c >= 127) {
                fprintf(out, "\\x%02x", (unsigned int)c);
            } else {
                fputc((int)c, out);
            }
            break;
        }
    }
}

static void pp_write_tok(FILE *out, struct tok *t)
{
    if (t->raw != NULL) {
        fputs(t->raw, out);
        return;
    }
    if (t->kind == TOK_NUM) {
        fprintf(out, "%ld", t->val);
    } else if (t->kind == TOK_FNUM) {
        fprintf(out, "%g", t->fval);
    } else if (t->kind == TOK_STR) {
        fputc('"', out);
        pp_write_escaped_str(out, t->str ? t->str : "");
        fputc('"', out);
    } else if (t->kind == TOK_CHAR_LIT) {
        if (t->val >= 32 && t->val < 127) {
            fprintf(out, "'%c'", (char)t->val);
        } else {
            fprintf(out, "'\\x%02x'", (int)t->val & 0xff);
        }
    } else if (t->str != NULL) {
        fprintf(out, "%s", t->str);
    } else {
        char tmp[16];
        tmp[0] = '\0';
        switch (t->kind) {
        case TOK_PLUS: strcpy(tmp, "+"); break;
        case TOK_MINUS: strcpy(tmp, "-"); break;
        case TOK_STAR: strcpy(tmp, "*"); break;
        case TOK_SLASH: strcpy(tmp, "/"); break;
        case TOK_PERCENT: strcpy(tmp, "%%"); break;
        case TOK_AMP: strcpy(tmp, "&"); break;
        case TOK_PIPE: strcpy(tmp, "|"); break;
        case TOK_CARET: strcpy(tmp, "^"); break;
        case TOK_TILDE: strcpy(tmp, "~"); break;
        case TOK_LSHIFT: strcpy(tmp, "<<"); break;
        case TOK_RSHIFT: strcpy(tmp, ">>"); break;
        case TOK_AND: strcpy(tmp, "&&"); break;
        case TOK_OR: strcpy(tmp, "||"); break;
        case TOK_NOT: strcpy(tmp, "!"); break;
        case TOK_EQ: strcpy(tmp, "=="); break;
        case TOK_NE: strcpy(tmp, "!="); break;
        case TOK_LT: strcpy(tmp, "<"); break;
        case TOK_GT: strcpy(tmp, ">"); break;
        case TOK_LE: strcpy(tmp, "<="); break;
        case TOK_GE: strcpy(tmp, ">="); break;
        case TOK_ASSIGN: strcpy(tmp, "="); break;
        case TOK_PLUS_EQ: strcpy(tmp, "+="); break;
        case TOK_MINUS_EQ: strcpy(tmp, "-="); break;
        case TOK_STAR_EQ: strcpy(tmp, "*="); break;
        case TOK_SLASH_EQ: strcpy(tmp, "/="); break;
        case TOK_PERCENT_EQ: strcpy(tmp, "%="); break;
        case TOK_AMP_EQ: strcpy(tmp, "&="); break;
        case TOK_PIPE_EQ: strcpy(tmp, "|="); break;
        case TOK_CARET_EQ: strcpy(tmp, "^="); break;
        case TOK_LSHIFT_EQ: strcpy(tmp, "<<="); break;
        case TOK_RSHIFT_EQ: strcpy(tmp, ">>="); break;
        case TOK_COMMA: strcpy(tmp, ","); break;
        case TOK_DOT: strcpy(tmp, "."); break;
        case TOK_ARROW: strcpy(tmp, "->"); break;
        case TOK_ELLIPSIS: strcpy(tmp, "..."); break;
        case TOK_LPAREN: strcpy(tmp, "("); break;
        case TOK_RPAREN: strcpy(tmp, ")"); break;
        case TOK_LBRACKET: strcpy(tmp, "["); break;
        case TOK_RBRACKET: strcpy(tmp, "]"); break;
        case TOK_QUESTION: strcpy(tmp, "?"); break;
        case TOK_COLON: strcpy(tmp, ":"); break;
        case TOK_HASH: strcpy(tmp, "#"); break;
        case TOK_PASTE: strcpy(tmp, "##"); break;
        case TOK_INC: strcpy(tmp, "++"); break;
        case TOK_DEC: strcpy(tmp, "--"); break;
        case TOK_SEMI: strcpy(tmp, ";"); break;
        case TOK_LBRACE: strcpy(tmp, "{"); break;
        case TOK_RBRACE: strcpy(tmp, "}"); break;
        default: strcpy(tmp, "?"); break;
        }
        fprintf(out, "%s", tmp);
    }
}

int pp_preprocess_to_file(const char *input_path, const char *output_path)
{
    FILE *inf;
    FILE *out;
    long sz;
    char *src;
    size_t nread;
    char *arena_buf;
    struct arena arena;
    struct tok *t;
    int need_space;

    inf = fopen(input_path, "rb");
    if (inf == NULL) {
        fprintf(stderr, "free-cc: cannot read '%s'\n", input_path);
        return 1;
    }
    fseek(inf, 0, SEEK_END);
    sz = ftell(inf);
    fseek(inf, 0, SEEK_SET);

    arena_buf = (char *)malloc(512UL * 1024 * 1024);
    if (arena_buf == NULL) {
        fclose(inf);
        return 1;
    }
    arena_init(&arena, arena_buf, 512UL * 1024 * 1024);

    src = (char *)arena_alloc(&arena, (usize)(sz + 2));
    nread = fread(src, 1, (size_t)sz, inf);
    fclose(inf);
    if (nread > 0 && src[nread - 1] != '\n') {
        src[nread] = '\n';
        nread++;
    }
    src[nread] = '\0';

    pp_init(src, input_path, &arena);

    if (output_path != NULL) {
        out = fopen(output_path, "w");
        if (out == NULL) {
            fprintf(stderr, "free-cc: cannot write '%s'\n", output_path);
            free(arena_buf);
            return 1;
        }
    } else {
        out = stdout;
    }

    need_space = 0;
    for (;;) {
        t = pp_next();
        if (t->kind == TOK_EOF) {
            break;
        }
        if (need_space) {
            fputc(' ', out);
        }
        need_space = 1;
        pp_write_tok(out, t);
        if (t->kind == TOK_SEMI || t->kind == TOK_LBRACE
            || t->kind == TOK_RBRACE) {
            fputc('\n', out);
            need_space = 0;
        }
    }
    fprintf(out, "\n");

    if (out != stdout) {
        fclose(out);
    }
    free(arena_buf);
    return 0;
}
