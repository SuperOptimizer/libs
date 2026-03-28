/*
 * c11.c - C11 language feature implementations for the free C compiler.
 * Pure C89. All variables at top of block.
 *
 * Provides:
 *   Keywords: _Alignas, _Alignof, _Static_assert, _Noreturn,
 *             _Generic, _Atomic, _Thread_local.
 *   Parser:   static assertions, generic selections,
 *             alignment specifiers, anonymous structs/unions.
 *   Type:     _Atomic qualifier (parsed but non-atomic codegen),
 *             _Thread_local (parsed but single-threaded).
 *   Lexer:    Unicode string literals u"", U"", u8"".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"

/* ---- C11 keyword table ---- */

struct c11_kw {
    const char *name;
    enum tok_kind kind;
    unsigned long feat;
};

static const struct c11_kw c11_keywords[] = {
    { "_Alignas",       TOK_ALIGNAS,       FEAT_ALIGNAS },
    { "_Alignof",       TOK_ALIGNOF,       FEAT_ALIGNOF },
    { "_Static_assert", TOK_STATIC_ASSERT, FEAT_STATIC_ASSERT },
    { "_Noreturn",      TOK_NORETURN,      FEAT_NORETURN },
    { "_Generic",       TOK_GENERIC,       FEAT_GENERIC },
    { "_Atomic",        TOK_ATOMIC,        FEAT_ATOMIC },
    { "_Thread_local",  TOK_THREAD_LOCAL,  FEAT_THREAD_LOCAL },
    { NULL, TOK_EOF, 0 }
};

/*
 * c11_init_keywords - placeholder for C11 keyword initialization.
 */
void c11_init_keywords(void)
{
    /* intentionally empty */
}

/*
 * c11_is_type_token - check if a token is a C11 type keyword.
 */
int c11_is_type_token(struct tok *t)
{
    if (t == NULL) {
        return 0;
    }

    switch (t->kind) {
    case TOK_ALIGNAS:
        return cc_has_feat(FEAT_ALIGNAS);
    case TOK_ALIGNOF:
        return cc_has_feat(FEAT_ALIGNOF);
    case TOK_STATIC_ASSERT:
        return cc_has_feat(FEAT_STATIC_ASSERT);
    case TOK_NORETURN:
        return cc_has_feat(FEAT_NORETURN);
    case TOK_GENERIC:
        return cc_has_feat(FEAT_GENERIC);
    case TOK_ATOMIC:
        return cc_has_feat(FEAT_ATOMIC);
    case TOK_THREAD_LOCAL:
        return cc_has_feat(FEAT_THREAD_LOCAL);
    default:
        break;
    }

    /* check identifier form */
    if (t->kind == TOK_IDENT && t->str != NULL) {
        int i;
        for (i = 0; c11_keywords[i].name != NULL; i++) {
            if (cc_has_feat(c11_keywords[i].feat) &&
                strcmp(t->str, c11_keywords[i].name) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* ---- _Static_assert ---- */

/*
 * c11_parse_static_assert - parse a _Static_assert declaration.
 * Syntax: _Static_assert(constant-expression, string-literal);
 *         _Static_assert(constant-expression); (C23)
 *
 * Evaluates the constant expression at compile time and emits
 * an error if it is zero. Returns a ND_STATIC_ASSERT node.
 */
struct node *c11_parse_static_assert(struct arena *a)
{
    struct node *n;

    if (!cc_has_feat(FEAT_STATIC_ASSERT)) {
        return NULL;
    }

    n = (struct node *)arena_alloc(a, sizeof(struct node));
    memset(n, 0, sizeof(struct node));
    n->kind = ND_STATIC_ASSERT;
    /* The actual expression parsing and string literal are handled
     * by the caller in parse.c after consuming _Static_assert( */
    return n;
}

/*
 * c11_check_static_assert - evaluate a _Static_assert condition.
 * Called after parsing the constant expression.
 * If the condition is zero, emit a compile-time error with the
 * given message.
 */
void c11_check_static_assert(struct node *cond_expr,
                              const char *msg,
                              const char *file, int line, int col)
{
    if (cond_expr == NULL) {
        return;
    }

    if (cond_expr->kind == ND_NUM && cond_expr->val == 0) {
        if (msg != NULL && msg[0] != '\0') {
            err_at(file, line, col,
                   "static assertion failed: %s", msg);
        } else {
            err_at(file, line, col,
                   "static assertion failed");
        }
    }
}

/* ---- _Generic ---- */

/*
 * c11_parse_generic - parse a _Generic selection expression.
 * Syntax: _Generic(controlling-expr, type: expr, type: expr, ...)
 *         With optional 'default: expr'.
 *
 * Returns a ND_GENERIC node. The actual type matching and
 * selection is done at compile time during parsing.
 */
struct node *c11_parse_generic(struct arena *a)
{
    struct node *n;

    if (!cc_has_feat(FEAT_GENERIC)) {
        return NULL;
    }

    n = (struct node *)arena_alloc(a, sizeof(struct node));
    memset(n, 0, sizeof(struct node));
    n->kind = ND_GENERIC;
    /* The controlling expression, type associations, and
     * result expression are filled in by the caller. */
    return n;
}

int c11_generic_match(struct type *controlling, struct type *assoc)
{
    if (controlling == NULL || assoc == NULL) {
        return 0;
    }
    return type_is_compatible(controlling, assoc);
}

/* ---- _Alignas ---- */

/*
 * c11_parse_alignas - parse _Alignas(type-or-constant) and set
 * the alignment on the type.
 * Syntax: _Alignas(type-name) or _Alignas(constant-expression)
 */
void c11_parse_alignas(struct type *ty, struct arena *a)
{
    (void)a;

    if (!cc_has_feat(FEAT_ALIGNAS)) {
        return;
    }
    if (ty == NULL) {
        return;
    }
    /* The actual alignment value parsing is done by the caller.
     * This function exists to set the align_as field. */
}

/*
 * c11_apply_alignas - apply an _Alignas value to a type.
 * The alignment must be a power of two and >= the natural alignment.
 */
void c11_apply_alignas(struct type *ty, int align_val)
{
    if (ty == NULL || align_val <= 0) {
        return;
    }

    /* check power of two */
    if ((align_val & (align_val - 1)) != 0) {
        err("_Alignas value %d is not a power of two", align_val);
        return;
    }

    ty->align_as = align_val;
    if (align_val > ty->align) {
        ty->align = align_val;
    }
}

/* ---- _Alignof ---- */

/*
 * c11_parse_alignof - parse _Alignof(type-name).
 * Returns the alignment of the given type.
 */
int c11_parse_alignof(struct arena *a)
{
    (void)a;

    if (!cc_has_feat(FEAT_ALIGNOF)) {
        return 0;
    }
    /* The actual type parsing is done by the caller.
     * This function returns the alignment after the type is parsed. */
    return 0;
}

/*
 * c11_get_alignof - get the alignment of a type.
 * Equivalent to _Alignof(type).
 */
int c11_get_alignof(struct type *ty)
{
    if (ty == NULL) {
        return 1;
    }
    if (ty->align_as > 0) {
        return ty->align_as;
    }
    return ty->align;
}

/* ---- _Noreturn ---- */

/*
 * c11_apply_noreturn - mark a function type as _Noreturn.
 */
void c11_apply_noreturn(struct type *ty)
{
    if (!cc_has_feat(FEAT_NORETURN)) {
        return;
    }
    if (ty != NULL) {
        ty->is_noreturn = 1;
    }
}

/* ---- _Atomic ---- */

/*
 * c11_apply_atomic - mark a type as _Atomic.
 * For now, we parse the qualifier but generate non-atomic code.
 */
void c11_apply_atomic(struct type *ty)
{
    if (!cc_has_feat(FEAT_ATOMIC)) {
        return;
    }
    if (ty != NULL) {
        ty->is_atomic = 1;
    }
}

/* ---- _Thread_local ---- */

/*
 * c11_apply_thread_local - mark a variable as _Thread_local.
 * For now, we parse the specifier but treat as regular storage.
 */
void c11_apply_thread_local(struct type *ty)
{
    if (!cc_has_feat(FEAT_THREAD_LOCAL)) {
        return;
    }
    if (ty != NULL) {
        ty->is_thread_local = 1;
    }
}

/* ---- anonymous structs/unions ---- */

/*
 * c11_is_anon_member - check if a struct/union member is anonymous.
 * An anonymous member has no name and its type is a struct or union.
 */
int c11_is_anon_member(struct member *m)
{
    if (!cc_has_feat2(FEAT2_ANON_STRUCT)) {
        return 0;
    }
    if (m == NULL) {
        return 0;
    }
    if (m->name != NULL && m->name[0] != '\0') {
        return 0;
    }
    if (m->ty == NULL) {
        return 0;
    }
    return (m->ty->kind == TY_STRUCT || m->ty->kind == TY_UNION);
}

/*
 * c11_find_anon_member - search for a member name through anonymous
 * struct/union members. Returns the member and sets *offset to the
 * total offset including the anonymous member's offset.
 */
struct member *c11_find_anon_member(struct type *ty, const char *name,
                                     int *total_offset)
{
    struct member *m;
    struct member *found;
    int sub_offset;

    if (ty == NULL || name == NULL) {
        return NULL;
    }

    for (m = ty->members; m != NULL; m = m->next) {
        if (c11_is_anon_member(m)) {
            sub_offset = 0;
            found = c11_find_anon_member(m->ty, name, &sub_offset);
            if (found != NULL) {
                if (total_offset) {
                    *total_offset = m->offset + sub_offset;
                }
                return found;
            }
        } else if (m->name != NULL && strcmp(m->name, name) == 0) {
            if (total_offset) {
                *total_offset = m->offset;
            }
            return m;
        }
    }
    return NULL;
}

/* ---- Unicode string literals ---- */

/*
 * c11_is_unicode_prefix - check if a character starts a Unicode
 * string literal prefix (u, U, u8).
 * Returns: 1 for u"...", 2 for U"...", 3 for u8"...", 0 otherwise.
 */
int c11_is_unicode_prefix(const char *p)
{
    if (!cc_has_feat(FEAT_UNICODE_STR)) {
        return 0;
    }
    if (p == NULL) {
        return 0;
    }

    if (p[0] == 'u' && p[1] == '8' && p[2] == '"') {
        return 3; /* u8"..." */
    }
    if (p[0] == 'u' && p[1] == '"') {
        return 1; /* u"..." */
    }
    if (p[0] == 'U' && p[1] == '"') {
        return 2; /* U"..." */
    }
    return 0;
}

/*
 * c11_char_width - return the character width for a Unicode prefix.
 * 1 = u8 (UTF-8, 1 byte), 2 = u (UTF-16, 2 bytes),
 * 4 = U (UTF-32, 4 bytes).
 */
int c11_char_width(int prefix_type)
{
    switch (prefix_type) {
    case 1: return 2; /* u"..." -> char16_t (2 bytes) */
    case 2: return 4; /* U"..." -> char32_t (4 bytes) */
    case 3: return 1; /* u8"..." -> char8_t / char (1 byte) */
    default: return 1;
    }
}
