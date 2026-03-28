/*
 * c23.c - C23 language feature implementations for the free C compiler.
 * Pure C89. All variables at top of block.
 *
 * Provides:
 *   Keywords: true, false, bool, nullptr, typeof, typeof_unqual,
 *             constexpr, static_assert (without message).
 *   Lexer:    Binary literals (0b1010), digit separators (1'000).
 *   Parser:   [[]] attribute syntax, empty initializers,
 *             labels before declarations, unnamed parameters.
 *   Type:     typeof / typeof_unqual operators.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"

/* ---- C23 keyword table ---- */

struct c23_kw {
    const char *name;
    enum tok_kind kind;
    unsigned long feat;
    unsigned long feat2; /* secondary feature word, 0 if first word */
};

static const struct c23_kw c23_keywords[] = {
    { "true",          TOK_TRUE,            FEAT_BOOL_KW, 0 },
    { "false",         TOK_FALSE,           FEAT_BOOL_KW, 0 },
    { "bool",          TOK_BOOL_KW,         FEAT_BOOL_KW, 0 },
    { "nullptr",       TOK_NULLPTR,         FEAT_NULLPTR,  0 },
    { "typeof",        TOK_TYPEOF,          FEAT_TYPEOF,   0 },
    { "typeof_unqual", TOK_TYPEOF_UNQUAL,   FEAT_TYPEOF,   0 },
    { "constexpr",     TOK_CONSTEXPR,       0, FEAT2_CONSTEXPR },
    { "static_assert", TOK_STATIC_ASSERT_KW,0, FEAT2_STATIC_ASSERT_NS },
    { NULL, TOK_EOF, 0, 0 }
};

/*
 * c23_init_keywords - placeholder for C23 keyword initialization.
 */
void c23_init_keywords(void)
{
    /* intentionally empty */
}

/*
 * c23_is_type_token - check if a token is a C23 type-related keyword.
 */
int c23_is_type_token(struct tok *t)
{
    if (t == NULL) {
        return 0;
    }

    switch (t->kind) {
    case TOK_TRUE:
    case TOK_FALSE:
        return cc_has_feat(FEAT_BOOL_KW);
    case TOK_BOOL_KW:
        return cc_has_feat(FEAT_BOOL_KW);
    case TOK_NULLPTR:
        return cc_has_feat(FEAT_NULLPTR);
    case TOK_TYPEOF:
    case TOK_TYPEOF_UNQUAL:
        return cc_has_feat(FEAT_TYPEOF);
    case TOK_CONSTEXPR:
        return cc_has_feat2(FEAT2_CONSTEXPR);
    case TOK_STATIC_ASSERT_KW:
        return cc_has_feat2(FEAT2_STATIC_ASSERT_NS);
    default:
        break;
    }

    /* check identifier form */
    if (t->kind == TOK_IDENT && t->str != NULL) {
        int i;
        for (i = 0; c23_keywords[i].name != NULL; i++) {
            if (strcmp(t->str, c23_keywords[i].name) == 0) {
                if (c23_keywords[i].feat != 0 &&
                    cc_has_feat(c23_keywords[i].feat)) {
                    return 1;
                }
                if (c23_keywords[i].feat2 != 0 &&
                    cc_has_feat2(c23_keywords[i].feat2)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* ---- binary literal parsing ---- */

/*
 * c23_parse_bin_literal - parse a binary integer literal (0b1010).
 * Returns 1 if successful, 0 otherwise.
 * Sets *val to the parsed value.
 */
int c23_parse_bin_literal(const char *s, long *val)
{
    const char *p;
    long result;

    if (!cc_has_feat(FEAT_BIN_LITERAL)) {
        return 0;
    }
    if (s == NULL) {
        return 0;
    }

    p = s;
    result = 0;

    /* check 0b or 0B prefix */
    if (p[0] != '0' || (p[1] != 'b' && p[1] != 'B')) {
        return 0;
    }
    p += 2;

    /* must have at least one binary digit */
    if (*p != '0' && *p != '1') {
        return 0;
    }

    while (*p == '0' || *p == '1' ||
           (*p == '\'' && cc_has_feat(FEAT_DIGIT_SEP))) {
        if (*p == '\'') {
            p++;
            continue; /* skip digit separator */
        }
        result = result * 2 + (*p - '0');
        p++;
    }

    if (val) {
        *val = result;
    }
    return 1;
}

/* ---- digit separator support ---- */

/*
 * c23_strip_digit_seps - parse an integer literal that may contain
 * digit separators ('). Returns the value.
 * The separator must appear between digits, not at start/end.
 */
long c23_strip_digit_seps(const char *s, int base)
{
    long result;
    const char *p;
    int d;

    if (!cc_has_feat(FEAT_DIGIT_SEP)) {
        return 0;
    }
    if (s == NULL) {
        return 0;
    }

    result = 0;
    p = s;

    while (*p != '\0') {
        if (*p == '\'') {
            p++;
            continue; /* skip separator */
        }

        d = -1;
        if (*p >= '0' && *p <= '9') {
            d = *p - '0';
        } else if (base == 16 && *p >= 'a' && *p <= 'f') {
            d = *p - 'a' + 10;
        } else if (base == 16 && *p >= 'A' && *p <= 'F') {
            d = *p - 'A' + 10;
        } else {
            break; /* not a digit for this base */
        }

        if (d >= base) {
            break;
        }
        result = result * base + d;
        p++;
    }

    return result;
}

/* ---- typeof / typeof_unqual ---- */

/*
 * c23_parse_typeof - parse typeof(expression) or typeof(type-name).
 * Returns the type of the expression or type-name.
 * The caller handles the parentheses.
 *
 * Note: The actual parsing is done by the caller in parse.c.
 * This function provides the node allocation.
 */
struct type *c23_parse_typeof(struct arena *a)
{
    (void)a;

    if (!cc_has_feat(FEAT_TYPEOF)) {
        return NULL;
    }
    /* The actual type resolution is done in parse.c.
     * This function exists for the API contract. */
    return NULL;
}

/*
 * c23_typeof_unqual - strip qualifiers from a type for typeof_unqual.
 * Returns a copy of the type without const/volatile/restrict/atomic.
 */
struct type *c23_typeof_unqual(struct type *ty, struct arena *a)
{
    struct type *copy;

    if (!cc_has_feat(FEAT_TYPEOF)) {
        return ty;
    }
    if (ty == NULL) {
        return NULL;
    }

    copy = (struct type *)arena_alloc(a, sizeof(struct type));
    memcpy(copy, ty, sizeof(struct type));
    copy->is_restrict = 0;
    copy->is_atomic = 0;
    /* const and volatile are not tracked as fields currently,
     * but if they were, we would clear them here. */
    return copy;
}

/* ---- [[]] attribute syntax ---- */

/* known C23 standard attributes */
#define C23_ATTR_NONE          0
#define C23_ATTR_NODISCARD     1
#define C23_ATTR_MAYBE_UNUSED  2
#define C23_ATTR_DEPRECATED    3
#define C23_ATTR_FALLTHROUGH   4
#define C23_ATTR_NORETURN      5
#define C23_ATTR_REPRODUCIBLE  6
#define C23_ATTR_UNSEQUENCED   7

/*
 * c23_parse_attribute - parse a [[ attribute-list ]] sequence.
 * Returns the attribute kind (C23_ATTR_*), or 0 if none/unknown.
 * The caller has already consumed [[ and will consume ]].
 *
 * Note: In practice, this is called from the parser which handles
 * the bracket tokens. This function identifies the attribute name.
 */
int c23_parse_attribute(struct arena *a)
{
    (void)a;

    if (!cc_has_feat(FEAT_ATTR_SYNTAX)) {
        return C23_ATTR_NONE;
    }
    /* The actual attribute name parsing is done by the caller.
     * This function exists for the API contract. */
    return C23_ATTR_NONE;
}

/*
 * c23_lookup_attribute - look up a standard attribute name.
 * Returns the attribute kind.
 */
int c23_lookup_attribute(const char *name)
{
    if (name == NULL) {
        return C23_ATTR_NONE;
    }

    if (strcmp(name, "nodiscard") == 0) {
        return C23_ATTR_NODISCARD;
    }
    if (strcmp(name, "maybe_unused") == 0) {
        return C23_ATTR_MAYBE_UNUSED;
    }
    if (strcmp(name, "deprecated") == 0) {
        return C23_ATTR_DEPRECATED;
    }
    if (strcmp(name, "fallthrough") == 0) {
        return C23_ATTR_FALLTHROUGH;
    }
    if (strcmp(name, "noreturn") == 0 ||
        strcmp(name, "_Noreturn") == 0) {
        return C23_ATTR_NORETURN;
    }
    if (strcmp(name, "reproducible") == 0) {
        return C23_ATTR_REPRODUCIBLE;
    }
    if (strcmp(name, "unsequenced") == 0) {
        return C23_ATTR_UNSEQUENCED;
    }

    return C23_ATTR_NONE;
}

/* ---- nullptr ---- */

/*
 * c23_is_nullptr - check if a token represents nullptr.
 */
int c23_is_nullptr(struct tok *t)
{
    if (!cc_has_feat(FEAT_NULLPTR)) {
        return 0;
    }
    if (t == NULL) {
        return 0;
    }
    if (t->kind == TOK_NULLPTR) {
        return 1;
    }
    if (t->kind == TOK_IDENT && t->str &&
        strcmp(t->str, "nullptr") == 0) {
        return 1;
    }
    return 0;
}

/* ---- constexpr ---- */

/*
 * c23_apply_constexpr - mark a variable declaration as constexpr.
 * The variable must be initialized with a constant expression.
 */
void c23_apply_constexpr(struct type *ty)
{
    if (!cc_has_feat2(FEAT2_CONSTEXPR)) {
        return;
    }
    if (ty != NULL) {
        ty->is_constexpr = 1;
    }
}

/*
 * c23_check_constexpr - verify that a constexpr initializer
 * is a constant expression. Returns 1 if valid.
 */
int c23_check_constexpr(struct node *init)
{
    if (init == NULL) {
        return 0;
    }
    /* a constant expression is either a number literal or
     * a string literal */
    if (init->kind == ND_NUM) {
        return 1;
    }
    if (init->kind == ND_STR) {
        return 1;
    }
    return 0;
}

/* ---- empty initializer ---- */

/*
 * c23_allow_empty_init - check if empty initializers (int x = {};)
 * are allowed in the current standard.
 */
int c23_allow_empty_init(void)
{
    return cc_has_feat2(FEAT2_EMPTY_INIT);
}

/* ---- labels before declarations ---- */

/*
 * c23_allow_label_decl - check if labels can appear before
 * declarations (label: int x = 0;).
 */
int c23_allow_label_decl(void)
{
    return cc_has_feat2(FEAT2_LABEL_DECL);
}

/* ---- unnamed parameters ---- */

/*
 * c23_allow_unnamed_param - check if unnamed parameters
 * in function definitions are allowed.
 */
int c23_allow_unnamed_param(void)
{
    return cc_has_feat2(FEAT2_UNNAMED_PARAM);
}

/* ---- bool keyword (C23 makes it a keyword, not a macro) ---- */

/*
 * c23_bool_type - create the C23 bool type.
 * In C23, 'bool' is a keyword (not _Bool), but it's the same type.
 */
struct type *c23_bool_type(struct arena *a)
{
    struct type *ty;

    if (!cc_has_feat(FEAT_BOOL_KW)) {
        return NULL;
    }

    ty = (struct type *)arena_alloc(a, sizeof(struct type));
    memset(ty, 0, sizeof(struct type));
    ty->kind = TY_BOOL;
    ty->size = 1;
    ty->align = 1;
    ty->is_unsigned = 1;
    ty->origin = ty;
    return ty;
}

/* ---- true/false constants ---- */

/*
 * c23_true_val - return the value of 'true' (always 1).
 */
long c23_true_val(void)
{
    return 1;
}

/*
 * c23_false_val - return the value of 'false' (always 0).
 */
long c23_false_val(void)
{
    return 0;
}
