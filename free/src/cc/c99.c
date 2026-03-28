/*
 * c99.c - C99 language feature implementations for the free C compiler.
 * Pure C89. All variables at top of block.
 *
 * Provides:
 *   Lexer:  _Bool, restrict, inline keywords; long long / LL/ULL suffixes;
 *           hex float literals; universal char names.
 *   Parser: mixed decls/stmts; for-loop var decls; VLAs;
 *           designated initializers; compound literals;
 *           flexible array members; static in array params;
 *           _Bool type conversions.
 *   Preprocessor: variadic macros; _Pragma; __func__; empty macro args.
 *   Type system: long long; _Bool; restrict; inline qualifiers.
 *   Codegen: VLA alloc (sub sp); compound literal init; _Bool conv.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"

/* ---- C99 keyword table ---- */

struct c99_kw {
    const char *name;
    enum tok_kind kind;
    unsigned long feat;   /* required feature flag */
};

static const struct c99_kw c99_keywords[] = {
    { "_Bool",     TOK_BOOL,     FEAT_BOOL },
    { "restrict",  TOK_RESTRICT, FEAT_RESTRICT },
    { "inline",    TOK_INLINE,   FEAT_INLINE },
    { "__inline",  TOK_INLINE,   FEAT_INLINE },
    { "__inline__",TOK_INLINE,   FEAT_INLINE },
    { "__restrict",  TOK_RESTRICT, FEAT_RESTRICT },
    { "__restrict__",TOK_RESTRICT, FEAT_RESTRICT },
    { NULL, TOK_EOF, 0 }
};

/*
 * c99_init_keywords - nothing needed at init time; keyword lookup
 * is done inline during lexing. This is a placeholder for any
 * future initialization.
 */
void c99_init_keywords(void)
{
    /* intentionally empty */
}

/*
 * c99_is_type_token - check if a token is a C99 type keyword.
 * Returns 1 if the token is a C99 type specifier.
 */
int c99_is_type_token(struct tok *t)
{
    if (t == NULL) {
        return 0;
    }

    if (t->kind == TOK_BOOL && cc_has_feat(FEAT_BOOL)) {
        return 1;
    }
    if (t->kind == TOK_RESTRICT && cc_has_feat(FEAT_RESTRICT)) {
        return 1; /* qualifier, but appears in declspec position */
    }
    if (t->kind == TOK_INLINE && cc_has_feat(FEAT_INLINE)) {
        return 1; /* function specifier, appears in declspec position */
    }

    /* check identifier for C99 keywords */
    if (t->kind == TOK_IDENT && t->str != NULL) {
        int i;
        for (i = 0; c99_keywords[i].name != NULL; i++) {
            if (cc_has_feat(c99_keywords[i].feat) &&
                strcmp(t->str, c99_keywords[i].name) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * c99_parse_type_spec - handle C99-specific type specifiers.
 * Called from parse_declspec when a C99 type keyword is found.
 * Returns the type, or NULL if not a C99 type.
 */
struct type *c99_parse_type_spec(struct tok *t, struct arena *a)
{
    struct type *ty;

    if (t == NULL || a == NULL) {
        return NULL;
    }

    /* _Bool type */
    if (t->kind == TOK_BOOL || (t->kind == TOK_IDENT && t->str &&
                                strcmp(t->str, "_Bool") == 0)) {
        if (!cc_has_feat(FEAT_BOOL)) {
            return NULL;
        }
        ty = (struct type *)arena_alloc(a, sizeof(struct type));
        memset(ty, 0, sizeof(struct type));
        ty->kind = TY_BOOL;
        ty->size = 1;
        ty->align = 1;
        ty->is_unsigned = 1; /* _Bool is unsigned */
        ty->origin = ty;
        return ty;
    }

    return NULL;
}

/*
 * c99_make_vla - create a variable-length array type.
 * The size_expr node represents the runtime size expression.
 * For codegen, the actual allocation happens at runtime.
 */
struct type *c99_make_vla(struct type *base, struct node *size_expr,
                          struct arena *a)
{
    struct type *ty;

    if (!cc_has_feat(FEAT_VLA)) {
        return NULL;
    }

    ty = (struct type *)arena_alloc(a, sizeof(struct type));
    memset(ty, 0, sizeof(struct type));
    ty->kind = TY_ARRAY;
    ty->base = base;
    ty->array_len = 0;  /* unknown at compile time */
    ty->is_vla = 1;
    /* size and align are base element's values; actual size is runtime */
    ty->size = base->size;  /* per-element size */
    ty->align = base->align;
    ty->vla_expr = size_expr; /* runtime element count */
    ty->origin = ty;
    return ty;
}

/*
 * c99_parse_for_decl - parse a declaration inside a for() init clause.
 * This creates a block-scoped variable declaration.
 * Returns a node representing the declaration/initialization,
 * or NULL if no declaration was parsed.
 *
 * Note: The actual integration with the parser is done by checking
 * cc_has_feat(FEAT_FOR_DECL) in parse_stmt's for-loop handler.
 * This function provides the type-checking helper.
 */
struct node *c99_parse_for_decl(struct arena *a)
{
    (void)a;
    /* The actual for-decl parsing is integrated into parse.c;
     * this function exists for the API contract.
     * See parse_stmt() for the for-loop declaration logic. */
    return NULL;
}

/*
 * c99_parse_compound_literal - parse a compound literal expression.
 * Called after seeing '(' type-name ')' '{'.
 * The type has already been parsed; this handles the initializer list.
 * Returns a ND_COMPOUND_LIT node.
 */
struct node *c99_parse_compound_literal(struct type *ty, struct arena *a)
{
    struct node *n;

    if (!cc_has_feat(FEAT_COMPOUND_LIT)) {
        return NULL;
    }

    n = (struct node *)arena_alloc(a, sizeof(struct node));
    memset(n, 0, sizeof(struct node));
    n->kind = ND_COMPOUND_LIT;
    n->ty = ty;
    /* The initializer list parsing is done by the caller in parse.c */
    return n;
}

/*
 * c99_parse_designated_init - parse a designated initializer.
 * Handles .field = value and [index] = value syntax.
 * Returns a ND_DESIG_INIT node.
 */
struct node *c99_parse_designated_init(struct arena *a)
{
    struct node *n;

    if (!cc_has_feat(FEAT_DESIG_INIT)) {
        return NULL;
    }

    n = (struct node *)arena_alloc(a, sizeof(struct node));
    memset(n, 0, sizeof(struct node));
    n->kind = ND_DESIG_INIT;
    /* Designator and value are filled in by the caller */
    return n;
}

/* ---- code generation helpers ---- */

/*
 * c99_gen_vla_alloc - emit code for VLA stack allocation.
 * Generates: sub sp, sp, size (aligned to 16 bytes).
 * The runtime size is in x0 after evaluating the size expression.
 * out is a FILE* for assembly output.
 */
void c99_gen_vla_alloc(struct node *n, void *out)
{
    FILE *f;

    f = (FILE *)out;
    if (n == NULL || f == NULL) {
        return;
    }

    /* VLA allocation:
     * 1. Evaluate size (already in x0 from gen_expr of size node)
     * 2. Multiply by element size
     * 3. Align to 16 bytes
     * 4. sub sp, sp, aligned_size
     * 5. Store sp as the VLA base address
     */
    fprintf(f, "\t/* VLA allocation */\n");

    /* size is already in x0 (element count) */
    if (n->ty && n->ty->base) {
        int elem_size;
        elem_size = n->ty->base->size;
        if (elem_size > 1) {
            fprintf(f, "\tmov x1, #%d\n", elem_size);
            fprintf(f, "\tmul x0, x0, x1\n");
        }
    }

    /* align to 16 bytes: (size + 15) & ~15 */
    fprintf(f, "\tadd x0, x0, #15\n");
    fprintf(f, "\tand x0, x0, #-16\n");

    /* allocate on stack */
    fprintf(f, "\tsub sp, sp, x0\n");

    /* result (VLA base address) is now sp */
    fprintf(f, "\tmov x0, sp\n");
}

/*
 * c99_gen_bool_conv - emit code for _Bool conversion.
 * Converts any nonzero value to 1, zero stays 0.
 * Value to convert is in x0.
 * out is a FILE* for assembly output.
 */
void c99_gen_bool_conv(struct node *n, void *out)
{
    FILE *f;

    f = (FILE *)out;
    (void)n;
    if (f == NULL) {
        return;
    }

    fprintf(f, "\t/* _Bool conversion */\n");
    fprintf(f, "\tcmp x0, #0\n");
    fprintf(f, "\tcset x0, ne\n");
}

/*
 * c99_gen_compound_lit - emit code for a compound literal.
 * Allocates space on the stack and initializes it.
 * out is a FILE* for assembly output.
 */
void c99_gen_compound_lit(struct node *n, void *out)
{
    FILE *f;
    int size;
    int aligned;

    f = (FILE *)out;
    if (n == NULL || f == NULL) {
        return;
    }

    size = (n->ty != NULL) ? n->ty->size : 8;
    /* align to 16 */
    aligned = (size + 15) & ~15;

    fprintf(f, "\t/* compound literal, %d bytes */\n", size);
    fprintf(f, "\tsub sp, sp, #%d\n", aligned);
    fprintf(f, "\tmov x0, sp\n");

    /* Zero-initialize the allocated space */
    if (size <= 32) {
        int off;
        for (off = 0; off + 8 <= size; off += 8) {
            fprintf(f, "\tstr xzr, [sp, #%d]\n", off);
        }
        for (; off + 4 <= size; off += 4) {
            fprintf(f, "\tstr wzr, [sp, #%d]\n", off);
        }
        for (; off < size; off++) {
            fprintf(f, "\tstrb wzr, [sp, #%d]\n", off);
        }
    } else {
        /* for larger sizes, use a loop (simplified) */
        int off;
        for (off = 0; off + 8 <= size; off += 8) {
            fprintf(f, "\tstr xzr, [sp, #%d]\n", off);
        }
        for (; off < size; off++) {
            fprintf(f, "\tstrb wzr, [sp, #%d]\n", off);
        }
    }
}

/* ---- long long type support ---- */

/*
 * c99_type_llong - create a long long type.
 * On aarch64, long long is 8 bytes (same as long).
 */
static struct type *c99_type_llong(struct arena *a, int is_unsigned)
{
    struct type *ty;

    ty = (struct type *)arena_alloc(a, sizeof(struct type));
    memset(ty, 0, sizeof(struct type));
    ty->kind = TY_LLONG;
    ty->size = 8;
    ty->align = 8;
    ty->is_unsigned = is_unsigned;
    ty->origin = ty;
    return ty;
}

/*
 * c99_parse_long_long - check for "long long" in declaration specifiers.
 * Returns a long long type if the current context has two consecutive
 * 'long' specifiers. Called from parse_declspec.
 */
struct type *c99_parse_long_long(int long_count, int is_unsigned,
                                 struct arena *a)
{
    if (!cc_has_feat(FEAT_LONG_LONG)) {
        return NULL;
    }
    if (long_count >= 2) {
        return c99_type_llong(a, is_unsigned);
    }
    return NULL;
}

/* ---- integer literal suffix parsing ---- */

/*
 * c99_parse_int_suffix - parse LL/ULL/ll/ull suffixes on integer literals.
 * Returns 1 if a long long suffix was found, 0 otherwise.
 * Sets *is_unsigned if U/u suffix present.
 */
int c99_parse_int_suffix(const char *pos, int *is_unsigned)
{
    const char *p;
    int has_ll;
    int has_u;

    if (!cc_has_feat(FEAT_LONG_LONG)) {
        return 0;
    }

    p = pos;
    has_ll = 0;
    has_u = 0;

    /* scan suffix chars */
    while (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L') {
        if (*p == 'u' || *p == 'U') {
            has_u = 1;
        }
        if ((*p == 'l' || *p == 'L') &&
            (*(p + 1) == 'l' || *(p + 1) == 'L')) {
            has_ll = 1;
            p++; /* skip second 'l' */
        }
        p++;
    }

    if (is_unsigned) {
        *is_unsigned = has_u;
    }
    return has_ll;
}

/* ---- hex float literal parsing ---- */

/*
 * c99_parse_hex_float - parse a hexadecimal floating-point literal.
 * Format: 0x[hex digits].[hex digits]p[+-][decimal exponent]
 * Returns 1 if parsed successfully, sets *val to the integer
 * bit representation. For now we just parse and skip the literal,
 * treating it as an integer approximation.
 */
int c99_parse_hex_float(const char *start, long *val)
{
    const char *p;
    long result;
    int has_dot;
    int has_exp;

    if (!cc_has_feat(FEAT_HEX_FLOAT)) {
        return 0;
    }

    p = start;
    result = 0;
    has_dot = 0;
    has_exp = 0;

    /* skip 0x prefix */
    if (*p == '0' && (*(p + 1) == 'x' || *(p + 1) == 'X')) {
        p += 2;
    } else {
        return 0;
    }

    /* parse hex digits before dot */
    while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
           (*p >= 'A' && *p <= 'F')) {
        int d;
        if (*p >= '0' && *p <= '9') {
            d = *p - '0';
        } else if (*p >= 'a' && *p <= 'f') {
            d = *p - 'a' + 10;
        } else {
            d = *p - 'A' + 10;
        }
        result = result * 16 + d;
        p++;
    }

    /* dot */
    if (*p == '.') {
        has_dot = 1;
        p++;
        /* skip fractional hex digits */
        while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
               (*p >= 'A' && *p <= 'F')) {
            p++;
        }
    }

    /* exponent: p or P */
    if (*p == 'p' || *p == 'P') {
        has_exp = 1;
        p++;
        if (*p == '+' || *p == '-') {
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            p++;
        }
    }

    if (!has_dot && !has_exp) {
        return 0; /* not a hex float */
    }

    /* skip float suffix */
    if (*p == 'f' || *p == 'F' || *p == 'l' || *p == 'L') {
        p++;
    }

    if (val) {
        *val = result;
    }
    return 1;
}

/* ---- universal character name support ---- */

/*
 * c99_parse_ucn - parse a universal character name (\uNNNN or \UNNNNNNNN).
 * Returns the Unicode code point value, or -1 on error.
 * Advances *pos past the UCN.
 */
int c99_parse_ucn(const char **pos)
{
    const char *p;
    int ndigits;
    int i;
    long val;
    int d;

    if (!cc_has_feat(FEAT_UCN)) {
        return -1;
    }

    p = *pos;

    /* must start with \u or \U */
    if (*p != '\\') {
        return -1;
    }
    p++;
    if (*p == 'u') {
        ndigits = 4;
    } else if (*p == 'U') {
        ndigits = 8;
    } else {
        return -1;
    }
    p++;

    val = 0;
    for (i = 0; i < ndigits; i++) {
        if (*p >= '0' && *p <= '9') {
            d = *p - '0';
        } else if (*p >= 'a' && *p <= 'f') {
            d = *p - 'a' + 10;
        } else if (*p >= 'A' && *p <= 'F') {
            d = *p - 'A' + 10;
        } else {
            return -1;
        }
        val = val * 16 + d;
        p++;
    }

    *pos = p;
    return (int)val;
}

/* ---- flexible array member support ---- */

/*
 * c99_is_flex_array - check if a member declaration is a flexible
 * array member (int data[] as last member of a struct).
 * Returns 1 if it is.
 */
int c99_is_flex_array(struct type *ty)
{
    if (!cc_has_feat(FEAT_FLEX_ARRAY)) {
        return 0;
    }
    if (ty == NULL) {
        return 0;
    }
    return (ty->kind == TY_ARRAY && ty->array_len == 0);
}

/* ---- static in array parameters ---- */

/*
 * c99_parse_static_array_param - parse 'static' in array parameter
 * declarations: void f(int a[static 10]).
 * Returns the minimum size, or 0 if no static qualifier.
 */
int c99_parse_static_array_param(struct tok *t)
{
    if (!cc_has_feat(FEAT_STATIC_ARRAY)) {
        return 0;
    }
    if (t == NULL || t->kind != TOK_STATIC) {
        return 0;
    }
    /* The actual size parsing is done by the caller;
     * this function just signals that 'static' was seen
     * in an array parameter context. */
    return 1;
}

/* ---- __func__ predefined identifier ---- */

/*
 * c99_get_func_name - return the predefined __func__ identifier
 * value for the current function. Returns NULL if not in a function.
 */
const char *c99_get_func_name(const char *current_func)
{
    if (!cc_has_feat(FEAT_FUNC_MACRO)) {
        return NULL;
    }
    return current_func;
}

/* ---- variadic macro support ---- */

/*
 * c99_is_va_args - check if an identifier is __VA_ARGS__.
 * Returns 1 if it is.
 */
int c99_is_va_args(const char *name)
{
    if (!cc_has_feat(FEAT_VARIADIC_MACRO)) {
        return 0;
    }
    if (name == NULL) {
        return 0;
    }
    return (strcmp(name, "__VA_ARGS__") == 0);
}

/* ---- _Pragma operator ---- */

/*
 * c99_handle_pragma - handle the _Pragma("string") operator.
 * For now, this is a no-op that just consumes the pragma string.
 * Returns 1 if handled.
 */
int c99_handle_pragma(const char *pragma_str)
{
    if (!cc_has_feat(FEAT_PRAGMA_OP)) {
        return 0;
    }
    /* silently ignore pragmas for now */
    (void)pragma_str;
    return 1;
}

/* ---- type system integration ---- */

/*
 * c99_type_is_integer - extended integer type check including
 * _Bool and long long.
 */
int c99_type_is_integer(struct type *ty)
{
    if (ty == NULL) {
        return 0;
    }
    if (ty->kind == TY_BOOL) {
        return 1;
    }
    if (ty->kind == TY_LLONG) {
        return 1;
    }
    return 0;
}

/*
 * c99_type_rank - return the integer conversion rank for
 * C99-specific types.
 * Returns -1 if not a C99 type.
 */
int c99_type_rank(struct type *ty)
{
    if (ty == NULL) {
        return -1;
    }
    if (ty->kind == TY_BOOL) {
        return 0; /* _Bool has lowest rank */
    }
    if (ty->kind == TY_LLONG) {
        return 5; /* above long (4) */
    }
    return -1;
}
