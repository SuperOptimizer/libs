/*
 * parse.c - Recursive descent parser for the free C compiler.
 * Pure C89. All variables at top of block.
 *
 * Parses a C89 translation unit into an AST built from struct node.
 * Uses the preprocessor (pp.c) for token input.
 */

#include "free.h"
#include <string.h>
#include <stdio.h>

/* ---- ext_attrs.c interface (GCC attributes / __extension__ / __typeof__) ---- */
extern int attr_is_attribute_keyword(const struct tok *t);
extern int attr_is_extension_keyword(const struct tok *t);
extern int attr_is_typeof_keyword(const struct tok *t);
extern int attr_is_auto_type(const struct tok *t);
extern void attr_init(struct arena *a);
struct attr_info {
    unsigned int flags;
    int aligned;
    int visibility;
    int vector_size;
    char *alias_name;
    char *section_name;
    char *cleanup_func;
    char *deprecated_msg;
    int format_archetype;
    int format_str_idx;
    int format_first_arg;
};
extern void attr_info_init(struct attr_info *info);
extern void attr_parse(struct attr_info *info, struct tok **tok_ptr);
extern int attr_try_parse(struct attr_info *info, struct tok **tok_ptr);
extern void attr_apply_to_type(struct type *ty, const struct attr_info *info);
extern int attr_is_noreturn_keyword(const struct tok *t);
extern int attr_skip_extension(struct tok **tok_ptr);
extern char *attr_parse_section_name(struct tok **tok_ptr);
extern void attr_set_const_eval(int (*cb)(struct tok **tok_ptr,
                                          long *out_val));

/* ---- forward declarations ---- */
static struct member *find_member_by_name(struct type *ty, const char *name);
static int init_list_has_index_designator(struct node *ilist);
static void set_inferred_array_len(struct type *ty, int len);
static int infer_unsized_array_len(struct type *ty, struct node *ilist);
static struct type *qualify_type(struct type *ty, int q_const,
                                 int q_volatile, int q_restrict,
                                 int q_atomic);
static int is_asm_qualifier(struct tok *t);

/* ---- ext_asm.c interface (inline assembly) ---- */
struct asm_stmt;
extern int asm_is_asm_keyword(const struct tok *t);
extern void asm_ext_init(struct arena *a);
extern void asm_parse(struct asm_stmt *stmt, struct tok **tok_ptr);
extern void asm_emit(FILE *out, const struct asm_stmt *stmt,
                     const char *func_name);
extern void asm_emit_basic(FILE *out, const char *tmpl);
extern void asm_set_const_eval(int (*cb)(struct tok **tok_ptr,
                                         long *out_val));
extern int asm_is_basic(const struct asm_stmt *stmt);
extern struct asm_stmt *asm_alloc_stmt(struct arena *a);
extern void asm_resolve_operands(struct asm_stmt *stmt,
                                 int (*lookup)(const char *name));

/* ---- preprocessor interface (from pp.c) ---- */
void pp_init(const char *src, const char *filename, struct arena *a);
struct tok *pp_next(void);
void pp_add_include_path(const char *path);

/* ---- type system interface (from type.c) ---- */
extern struct type *ty_void;
extern struct type *ty_char;
extern struct type *ty_short;
extern struct type *ty_int;
extern struct type *ty_long;
extern struct type *ty_schar;
extern struct type *ty_uchar;
extern struct type *ty_ushort;
extern struct type *ty_uint;
extern struct type *ty_ulong;
extern struct type *ty_float;
extern struct type *ty_double;
/* long double (provided by type.c once supported). Keep this weak so the
 * compiler still links before the type layer grows a distinct long double. */
extern struct type *ty_ldouble __attribute__((weak));
/* C99 types */
extern struct type *ty_bool;
extern struct type *ty_llong;
extern struct type *ty_ullong;
/* C99 complex types */
extern struct type *ty_cfloat;
extern struct type *ty_cdouble;
/* GCC __int128 */
extern struct type *ty_int128;
extern struct type *ty_uint128;

void type_init(struct arena *a);
struct type *type_ptr(struct type *base);
struct type *type_array(struct type *base, int len);
struct type *type_func(struct type *ret, struct type *params);
struct type *type_enum(void);
int type_size(struct type *ty);
int type_align(struct type *ty);
int type_is_integer(struct type *ty);
int type_is_flonum(struct type *ty);
int type_is_pointer(struct type *ty);
int type_is_numeric(struct type *ty);
int type_is_compatible(struct type *a, struct type *b);
struct type *type_common(struct type *a, struct type *b);
void type_complete_struct(struct type *ty);
void type_complete_union(struct type *ty);
struct member *type_find_member(struct type *ty, const char *name);
struct member *type_add_member(struct type *ty, const char *name,
                               struct type *mty);

/* sentinel type for __auto_type — type inferred from initializer */
static struct type ty_auto_type_sentinel = { TY_VOID, 0, 0, 0, NULL, 0, NULL,
    NULL, NULL, NULL, NULL, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    NULL };

/* ---- parser limits ---- */
#define MAX_SCOPE_DEPTH  256
#define MAX_TYPEDEFS     16384
#define MAX_TAGS         16384
#define MAX_ENUM_VALS    16384
#define MAX_SWITCH_DEPTH 32
#define MAX_LABELS       8192

/* ---- scope / symbol table ---- */
struct scope {
    struct symbol *locals;
    struct scope *parent;
    int is_func_boundary; /* 1 if this scope is a nested function boundary */
};

/* typedef registry */
struct typedef_entry {
    char *name;
    struct type *ty;
    struct scope *scope;
    int scope_depth;
};

/* struct/union/enum tag registry */
struct tag_entry {
    char *name;
    struct type *ty;
    struct scope *scope;
    int scope_depth;
};

/* enum constant */
struct enum_val {
    char *name;
    long val;
};

/* ---- static state ---- */
static struct arena *parse_arena;
static struct tok *cur_tok;
static struct scope *cur_scope;
static int scope_depth;
static struct attr_info *declspec_attrs; /* set by parse_toplevel for parse_declspec */
static struct attr_info declarator_attrs; /* attrs parsed by parse_declarator */

static struct symbol *globals;

static struct typedef_entry typedefs[MAX_TYPEDEFS];
static int ntypedefs;

static struct tag_entry tags[MAX_TAGS];
static int ntags;

static struct enum_val enum_vals[MAX_ENUM_VALS];
static int nenum_vals;

static int label_counter;
static int local_offset;       /* current stack offset for locals */

/* static local variables: emitted as globals with mangled names */
static struct node *static_locals;
static struct node *static_locals_tail;
static int static_local_counter;

/* nested function definitions: emitted as top-level functions */
static struct node *nested_funcs;
static struct node *nested_funcs_tail;

/* for break/continue targets */
static int brk_label;
static int cont_label;

/* return type of the current function (for inserting return casts) */
static struct type *current_ret_type;

/* name of the current function (for __func__, __FUNCTION__) */
static const char *current_func_name;
/* enclosing function name (for nested function label resolution) */
static const char *enclosing_func_name;

/* labels declared via __label__ in the enclosing function scope */
#define MAX_ENCLOSING_LABELS 64
static const char *enclosing_labels[MAX_ENCLOSING_LABELS];
static int n_enclosing_labels;

static int is_enclosing_label(const char *name)
{
    int i;
    if (enclosing_func_name == NULL) return 0;
    for (i = 0; i < n_enclosing_labels; i++) {
        if (strcmp(enclosing_labels[i], name) == 0) return 1;
    }
    return 0;
}

/* for switch */
static struct node *cur_switch;

/* ---- helpers ---- */

static struct tok *peek(void)
{
    return cur_tok;
}

/* saved token for 1-token lookahead pushback */
static struct tok *pushback_tok;

static struct tok *advance(void)
{
    struct tok *t;

    t = cur_tok;
    if (pushback_tok) {
        cur_tok = pushback_tok;
        pushback_tok = NULL;
    } else {
        cur_tok = pp_next();
    }
    return t;
}

static void unadvance(struct tok *t)
{
    pushback_tok = cur_tok;
    cur_tok = t;
}

static struct tok *expect(enum tok_kind kind, const char *msg)
{
    struct tok *t;

    t = cur_tok;
    if (t->kind != kind) {
        diag_error(t->file, t->line, t->col,
                   "expected %s, got token kind %d",
                   msg, (int)t->kind);
        /* don't advance past the unexpected token so callers can recover */
        return t;
    }
    return advance();
}

/* ---- error recovery helpers ---- */

/*
 * skip_to_next_decl - skip tokens until the next top-level declaration
 * boundary: ';', '}' at depth 0, or EOF.
 * Used for recovering from errors in top-level parsing.
 */
static void skip_to_next_decl(void)
{
    int depth;

    depth = 0;
    while (peek()->kind != TOK_EOF) {
        if (peek()->kind == TOK_LBRACE) {
            depth++;
        } else if (peek()->kind == TOK_RBRACE) {
            if (depth <= 0) {
                advance();
                return;
            }
            depth--;
        } else if (peek()->kind == TOK_SEMI && depth == 0) {
            advance();
            return;
        }
        advance();
    }
}

/*
 * skip_attribute - skip __attribute__((...)) from parser token stream.
 * Must be called when cur_tok points at __attribute__ identifier.
 */
static void skip_attribute(void)
{
    int depth;

    advance(); /* consume __attribute__/__attribute */
    if (peek()->kind != TOK_LPAREN) return;
    advance(); /* consume first '(' */
    depth = 1;
    while (depth > 0 && peek()->kind != TOK_EOF) {
        if (peek()->kind == TOK_LPAREN) depth++;
        else if (peek()->kind == TOK_RPAREN) depth--;
        advance();
    }
}

/*
 * skip_braces - skip a brace-enclosed initializer list { ... }.
 * Handles nested braces.
 */
static void skip_braces(void)
{
    int depth;

    if (peek()->kind != TOK_LBRACE) return;
    advance(); /* consume '{' */
    depth = 1;
    while (depth > 0 && peek()->kind != TOK_EOF) {
        if (peek()->kind == TOK_LBRACE) depth++;
        else if (peek()->kind == TOK_RBRACE) depth--;
        if (depth > 0) advance();
    }
    if (peek()->kind == TOK_RBRACE) {
        advance(); /* consume final '}' */
    }
}

/*
 * is_gcc_shorthand_attr - return 1 if the identifier is a GCC
 * standalone attribute shorthand like __packed or __aligned.
 */
static int is_gcc_shorthand_attr(const struct tok *t)
{
    if (t->kind != TOK_IDENT || t->str == NULL) return 0;
    return strcmp(t->str, "__packed") == 0 ||
           strcmp(t->str, "__packed__") == 0 ||
           strcmp(t->str, "__aligned") == 0 ||
           strcmp(t->str, "__aligned__") == 0 ||
           strcmp(t->str, "__transparent_union") == 0 ||
           strcmp(t->str, "__transparent_union__") == 0 ||
           strcmp(t->str, "__deprecated") == 0 ||
           strcmp(t->str, "__deprecated__") == 0;
}

/*
 * skip_all_attrs_and_ext - skip any combination of __attribute__,
 * __extension__, GCC shorthand attrs (__packed etc.), and asm(...)
 * after declarations/declarators.
 */
/* skip_c23_attr - skip C23 [[ ... ]] attribute.
 * Returns 1 if skipped, 0 if no [[ found.
 * Must be called when peek() is TOK_LBRACKET. */
static int skip_c23_attr(void)
{
    static struct tok saved;
    int depth;

    if (peek()->kind != TOK_LBRACKET) return 0;
    saved = *peek();
    advance(); /* consume first [ */
    if (peek()->kind != TOK_LBRACKET) {
        unadvance(&saved);
        return 0;
    }
    advance(); /* consume second [ */
    depth = 1;
    while (depth > 0 && peek()->kind != TOK_EOF) {
        if (peek()->kind == TOK_RBRACKET) {
            advance();
            if (peek()->kind == TOK_RBRACKET) {
                depth--;
                if (depth > 0) advance();
            }
        } else {
            advance();
        }
    }
    if (peek()->kind == TOK_RBRACKET) advance();
    return 1;
}

/* ---- kernel raw annotation identifiers ---- */

static int is_kernel_annot(const struct tok *t)
{
    const char *s;

    if (t == NULL || t->kind != TOK_IDENT || t->str == NULL) {
        return 0;
    }
    s = t->str;

    /* Keep this list narrow and explicit: these show up in the kernel as
     * raw identifiers in declarations and type casts after preprocessing. */
    return strcmp(s, "__always_inline") == 0
        || strcmp(s, "__must_check") == 0
        || strcmp(s, "__attribute_const__") == 0
        || strcmp(s, "__cold") == 0
        || strcmp(s, "__compiletime_error") == 0
        || strcmp(s, "__section") == 0
        || strcmp(s, "__no_context_analysis") == 0
        || strcmp(s, "__noreturn") == 0
        || strcmp(s, "__acquires") == 0
        || strcmp(s, "__releases") == 0
        || strcmp(s, "__acquires_shared") == 0
        || strcmp(s, "__releases_shared") == 0
        || strcmp(s, "__cond_acquires") == 0
        || strcmp(s, "__force") == 0
        || strcmp(s, "__user") == 0
        || strcmp(s, "__iomem") == 0
        || strcmp(s, "__percpu") == 0
        || strcmp(s, "__rcu") == 0
        || strcmp(s, "__bitwise") == 0
        || strcmp(s, "__private") == 0
        || strcmp(s, "__safe") == 0
        || strcmp(s, "__nocast") == 0;
}

static void skip_kernel_annot(void)
{
    int depth;

    if (!is_kernel_annot(peek())) {
        return;
    }
    advance(); /* consume the annotation identifier */

    /* Some annotations take a simple argument list, e.g.:
     *   __section(".init.text")
     *   __acquires(lock)
     * Skip balanced parentheses conservatively. */
    if (peek()->kind == TOK_LPAREN) {
        advance(); /* consume '(' */
        depth = 1;
        while (depth > 0 && peek()->kind != TOK_EOF) {
            if (peek()->kind == TOK_LPAREN) depth++;
            else if (peek()->kind == TOK_RPAREN) depth--;
            advance();
        }
    }
}

static void skip_all_attrs_and_ext(void)
{
    for (;;) {
        /* C23 [[ ... ]] attribute */
        if (peek()->kind == TOK_LBRACKET) {
            if (skip_c23_attr()) continue;
            break;
        }
        if (peek()->kind == TOK_IDENT && peek()->str) {
            /* Linux kernel raw annotation identifiers */
            if (is_kernel_annot(peek())) {
                skip_kernel_annot();
                continue;
            }
            if (attr_is_attribute_keyword(peek())) {
                skip_attribute();
                continue;
            }
            if (attr_is_extension_keyword(peek())) {
                advance();
                continue;
            }
            if (is_gcc_shorthand_attr(peek())) {
                advance();
                /* skip optional parenthesized arg: __aligned(N) */
                if (peek()->kind == TOK_LPAREN) {
                    int d;
                    advance();
                    d = 1;
                    while (d > 0 && peek()->kind != TOK_EOF) {
                        if (peek()->kind == TOK_LPAREN) d++;
                        else if (peek()->kind == TOK_RPAREN) d--;
                        advance();
                    }
                }
                continue;
            }
            if (asm_is_asm_keyword(peek())) {
                /* skip asm("symbol_name") on declarations */
                int depth;
                advance(); /* consume asm */
                if (peek()->kind == TOK_LPAREN) {
                    advance();
                    depth = 1;
                    while (depth > 0 && peek()->kind != TOK_EOF) {
                        if (peek()->kind == TOK_LPAREN) depth++;
                        else if (peek()->kind == TOK_RPAREN) depth--;
                        advance();
                    }
                }
                continue;
            }
        }
        break;
    }
}

static int new_label(void)
{
    return label_counter++;
}

static struct node *new_node(enum node_kind kind)
{
    struct node *n;

    n = (struct node *)arena_alloc(parse_arena, sizeof(struct node));
    memset(n, 0, sizeof(struct node));
    n->kind = kind;
    return n;
}

static struct node *new_num(long val)
{
    struct node *n;

    n = new_node(ND_NUM);
    n->val = val;
    n->ty = ty_int;
    return n;
}

static struct node *new_ulong(long val)
{
    struct node *n;

    n = new_node(ND_NUM);
    n->val = val;
    n->ty = ty_ulong;
    return n;
}

static struct node *new_binary(enum node_kind kind, struct node *lhs,
                               struct node *rhs)
{
    struct node *n;

    n = new_node(kind);
    n->lhs = lhs;
    n->rhs = rhs;
    return n;
}

static struct node *new_unary(enum node_kind kind, struct node *operand)
{
    struct node *n;

    n = new_node(kind);
    n->lhs = operand;
    return n;
}

/* ---- constant folding ---- */

/*
 * constant_fold - simplify constant expressions at parse time.
 * Folds arithmetic, bitwise, logical, comparison, ternary,
 * and cast operations on ND_NUM nodes.
 */
struct node *constant_fold(struct node *n)
{
    long a, b, r;

    if (n == NULL) {
        return n;
    }

    /* recursively fold children first */
    n->lhs = constant_fold(n->lhs);
    n->rhs = constant_fold(n->rhs);
    n->cond = constant_fold(n->cond);
    n->then_ = constant_fold(n->then_);
    n->els = constant_fold(n->els);

    /* ternary with constant condition */
    if (n->kind == ND_TERNARY && n->cond != NULL &&
        n->cond->kind == ND_NUM) {
        struct node *result_branch;
        result_branch = n->cond->val ? n->then_ : n->els;
        /* Preserve the ternary's type (which has usual arithmetic
         * conversions applied) by wrapping in a cast if needed. */
        if (result_branch != NULL && n->ty != NULL &&
            result_branch->ty != NULL &&
            result_branch->ty != n->ty) {
            struct node *cast;
            cast = new_unary(ND_CAST, result_branch);
            cast->ty = n->ty;
            return cast;
        }
        return result_branch;
    }

    /* unary operations on constants */
    if (n->kind == ND_BITNOT && n->lhs != NULL &&
        n->lhs->kind == ND_NUM) {
        long v;
        struct node *res;
        v = ~n->lhs->val;
        /* truncate to type width */
        if (n->lhs->ty != NULL) {
            if (n->lhs->ty->size == 1) {
                v = n->lhs->ty->is_unsigned
                    ? (long)(unsigned char)v : (long)(signed char)v;
            } else if (n->lhs->ty->size == 2) {
                v = n->lhs->ty->is_unsigned
                    ? (long)(unsigned short)v : (long)(short)v;
            } else if (n->lhs->ty->size == 4) {
                v = n->lhs->ty->is_unsigned
                    ? (long)(unsigned int)v : (long)(int)v;
            }
        }
        res = new_num(v);
        res->ty = n->lhs->ty;
        return res;
    }
    if (n->kind == ND_LOGNOT && n->lhs != NULL &&
        n->lhs->kind == ND_NUM) {
        return new_num(!n->lhs->val);
    }

    /* negation: ND_SUB(0, const) -> ND_NUM(-const) */
    if (n->kind == ND_SUB && n->lhs != NULL && n->rhs != NULL &&
        n->lhs->kind == ND_NUM && n->lhs->val == 0 &&
        n->rhs->kind == ND_NUM) {
        struct node *res;
        res = new_num(-n->rhs->val);
        res->ty = n->ty ? n->ty : n->rhs->ty;
        return res;
    }

    /* float negation: ND_SUB(0, fnum) -> ND_FNUM(-fval)
     * Preserves -0.0 per IEEE 754. */
    if (n->kind == ND_SUB && n->lhs != NULL && n->rhs != NULL &&
        n->lhs->kind == ND_NUM && n->lhs->val == 0 &&
        n->rhs->kind == ND_FNUM) {
        n->rhs->fval = -n->rhs->fval;
        return n->rhs;
    }

    /* float constant folding: both operands are ND_FNUM */
    if (n->lhs != NULL && n->rhs != NULL &&
        n->lhs->kind == ND_FNUM && n->rhs->kind == ND_FNUM) {
        double a_f, b_f, res_f;
        int both_float;
        a_f = n->lhs->fval;
        b_f = n->rhs->fval;
        /* if both operands are float type, truncate result */
        both_float = (n->lhs->ty != NULL &&
                      n->lhs->ty->kind == TY_FLOAT &&
                      n->rhs->ty != NULL &&
                      n->rhs->ty->kind == TY_FLOAT);
        res_f = 0;
        switch (n->kind) {
        case ND_ADD: res_f = a_f + b_f; break;
        case ND_SUB: res_f = a_f - b_f; break;
        case ND_MUL: res_f = a_f * b_f; break;
        case ND_DIV:
            if (b_f != 0.0) {
                res_f = a_f / b_f;
            } else {
                goto no_fold;
            }
            break;
        default: goto no_fold;
        }
        if (both_float) {
            n->lhs->fval = (float)res_f;
            n->lhs->ty = ty_float;
        } else {
            n->lhs->fval = res_f;
        }
        return n->lhs;
    no_fold: ;
    }

    /* cast of constant */
    if (n->kind == ND_CAST && n->lhs != NULL &&
        n->lhs->kind == ND_NUM && n->ty != NULL) {
        long v;
        struct node *res;
        /* cast integer constant to float/double -> ND_FNUM */
        if (n->ty->kind == TY_FLOAT || n->ty->kind == TY_DOUBLE) {
            double fv;
            if (n->lhs->ty != NULL && n->lhs->ty->is_unsigned) {
                fv = (double)(unsigned long)n->lhs->val;
            } else {
                fv = (double)n->lhs->val;
            }
            res = new_node(ND_FNUM);
            if (n->ty->kind == TY_FLOAT) {
                res->fval = (float)fv;
            } else {
                res->fval = fv;
            }
            res->label_id = label_counter++;
            res->ty = n->ty;
            return res;
        }
        v = n->lhs->val;
        if (n->ty->size == 1) {
            v = n->ty->is_unsigned
                ? (long)(unsigned char)v : (long)(signed char)v;
        } else if (n->ty->size == 2) {
            v = n->ty->is_unsigned
                ? (long)(unsigned short)v : (long)(short)v;
        } else if (n->ty->size == 4) {
            v = n->ty->is_unsigned
                ? (long)(unsigned int)v : (long)(int)v;
        }
        res = new_num(v);
        res->ty = n->ty;
        return res;
    }

    /* cast of float constant to integer */
    if (n->kind == ND_CAST && n->lhs != NULL &&
        n->lhs->kind == ND_FNUM && n->ty != NULL &&
        n->ty->kind != TY_FLOAT && n->ty->kind != TY_DOUBLE) {
        long v;
        struct node *res;
        if (n->ty->is_unsigned) {
            v = (long)(unsigned long)n->lhs->fval;
        } else {
            v = (long)n->lhs->fval;
        }
        if (n->ty->size == 1) {
            v = n->ty->is_unsigned
                ? (long)(unsigned char)v : (long)(signed char)v;
        } else if (n->ty->size == 2) {
            v = n->ty->is_unsigned
                ? (long)(unsigned short)v : (long)(short)v;
        } else if (n->ty->size == 4) {
            v = n->ty->is_unsigned
                ? (long)(unsigned int)v : (long)(int)v;
        }
        res = new_num(v);
        res->ty = n->ty;
        return res;
    }

    /* cast of float constant to different float type */
    if (n->kind == ND_CAST && n->lhs != NULL &&
        n->lhs->kind == ND_FNUM && n->ty != NULL &&
        (n->ty->kind == TY_FLOAT || n->ty->kind == TY_DOUBLE)) {
        struct node *res;
        res = new_node(ND_FNUM);
        if (n->ty->kind == TY_FLOAT) {
            res->fval = (float)n->lhs->fval;
        } else {
            res->fval = n->lhs->fval;
        }
        res->label_id = label_counter++;
        res->ty = n->ty;
        return res;
    }

    /* binary operations: both operands must be ND_NUM */
    if (n->lhs == NULL || n->rhs == NULL) {
        return n;
    }
    if (n->lhs->kind != ND_NUM || n->rhs->kind != ND_NUM) {
        return n;
    }

    a = n->lhs->val;
    b = n->rhs->val;
    r = 0;

    /* check if either operand type is unsigned for comparison/div/mod/shr */
    {
    int is_unsigned;
    int common_size;
    unsigned long ua, ub;
    is_unsigned = (n->lhs->ty && n->lhs->ty->is_unsigned) ||
                  (n->rhs->ty && n->rhs->ty->is_unsigned);

    /* determine common type size for proper truncation */
    common_size = 8;
    if (n->ty && n->ty->size < 8) {
        common_size = n->ty->size;
    } else {
        struct type *ct;
        ct = type_common(n->lhs->ty, n->rhs->ty);
        if (ct != NULL && ct->size < 8) common_size = ct->size;
    }

    /* truncate to common type width for unsigned operations */
    ua = (unsigned long)a;
    ub = (unsigned long)b;
    if (is_unsigned && common_size <= 4) {
        ua = ua & 0xFFFFFFFFUL;
        ub = ub & 0xFFFFFFFFUL;
    }

    switch (n->kind) {
    case ND_ADD:    r = a + b; break;
    case ND_SUB:    r = a - b; break;
    case ND_MUL:    r = a * b; break;
    case ND_DIV:
        if (b == 0) return n;
        if (is_unsigned)
            r = (long)(ua / ub);
        else
            r = a / b;
        break;
    case ND_MOD:
        if (b == 0) return n;
        if (is_unsigned)
            r = (long)(ua % ub);
        else
            r = a % b;
        break;
    case ND_BITAND: r = a & b; break;
    case ND_BITOR:  r = a | b; break;
    case ND_BITXOR: r = a ^ b; break;
    case ND_SHL:
        if (is_unsigned)
            r = (long)(ua << ub);
        else
            r = a << b;
        break;
    case ND_SHR:
        if (is_unsigned)
            r = (long)(ua >> ub);
        else
            r = a >> b;
        break;
    case ND_EQ:
        if (is_unsigned)
            r = (ua == ub);
        else
            r = (a == b);
        break;
    case ND_NE:
        if (is_unsigned)
            r = (ua != ub);
        else
            r = (a != b);
        break;
    case ND_LT:
        if (is_unsigned)
            r = (ua < ub);
        else
            r = (a < b);
        break;
    case ND_LE:
        if (is_unsigned)
            r = (ua <= ub);
        else
            r = (a <= b);
        break;
    case ND_LOGAND: r = (a && b); break;
    case ND_LOGOR:  r = (a || b); break;
    default:
        return n;
    }
    }

    /* truncate result to the expression type width */
    {
        struct type *rty;
        rty = n->ty ? n->ty : (n->lhs ? n->lhs->ty : NULL);
        if (rty != NULL) {
            if (rty->size == 4) {
                if (rty->is_unsigned) {
                    r = (long)(unsigned int)r;
                } else {
                    r = (long)(int)r;
                }
            } else if (rty->size == 2) {
                if (rty->is_unsigned) {
                    r = (long)(unsigned short)r;
                } else {
                    r = (long)(short)r;
                }
            } else if (rty->size == 1) {
                if (rty->is_unsigned) {
                    r = (long)(unsigned char)r;
                } else {
                    r = (long)(signed char)r;
                }
            }
        }
    }

    {
        struct node *res;
        res = new_num(r);
        res->ty = n->ty ? n->ty : n->lhs->ty;
        return res;
    }
}

/* ---- scope management ---- */

static void enter_scope(void)
{
    struct scope *s;

    s = (struct scope *)arena_alloc(parse_arena, sizeof(struct scope));
    s->locals = NULL;
    s->parent = cur_scope;
    s->is_func_boundary = 0;
    cur_scope = s;
    scope_depth++;
}

static void leave_scope(void)
{
    if (cur_scope != NULL) {
        cur_scope = cur_scope->parent;
    }
    scope_depth--;
}

static struct symbol *find_local(const char *name)
{
    struct scope *s;
    struct symbol *sym;

    for (s = cur_scope; s != NULL; s = s->parent) {
        for (sym = s->locals; sym != NULL; sym = sym->next) {
            if (strcmp(sym->name, name) == 0) {
                return sym;
            }
        }
    }
    return NULL;
}

/*
 * find_local_ex - like find_local but also reports whether the symbol
 * was found beyond a nested function boundary (i.e. in the enclosing
 * function's scope). Sets *is_upvar to 1 if so, 0 otherwise.
 */
static struct symbol *find_local_ex(const char *name, int *is_upvar)
{
    struct scope *s;
    struct symbol *sym;
    int crossed_boundary;

    crossed_boundary = 0;
    *is_upvar = 0;

    for (s = cur_scope; s != NULL; s = s->parent) {
        for (sym = s->locals; sym != NULL; sym = sym->next) {
            if (strcmp(sym->name, name) == 0) {
                *is_upvar = crossed_boundary;
                return sym;
            }
        }
        if (s->is_func_boundary) {
            crossed_boundary = 1;
        }
    }
    return NULL;
}

static struct symbol *find_global(const char *name)
{
    struct symbol *sym;

    for (sym = globals; sym != NULL; sym = sym->next) {
        if (strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

static struct symbol *find_symbol(const char *name)
{
    struct symbol *sym;

    sym = find_local(name);
    if (sym != NULL) {
        return sym;
    }
    return find_global(name);
}

/* forward declarations needed by asm_const_expr_eval */
static struct node *parse_expr(void);
static void add_type(struct node *n);

/* callback for asm_resolve_operands: returns positive frame
 * offset for local variables, 0 for globals.
 * Sets *is_upvar_out to 1 if the variable is from the enclosing
 * function scope (nested function upvar access). */
int asm_var_is_upvar;
static int asm_var_lookup(const char *name)
{
    struct symbol *sym;
    int is_upvar;
    is_upvar = 0;
    sym = find_local_ex(name, &is_upvar);
    if (sym != NULL && sym->offset > 0) {
        asm_var_is_upvar = is_upvar;
        return sym->offset;
    }
    asm_var_is_upvar = 0;
    return 0;
}

/*
 * asm_const_expr_eval - callback for ext_asm.c to evaluate constant
 * expressions in "i"/"n" constraint operands.
 * The tok_ptr points to the '(' token. We consume through ')'.
 * Returns 1 on success with value in *out_val, 0 on failure.
 */
static int asm_const_expr_eval(struct tok **tok_ptr, long *out_val)
{
    struct node *n;
    int ok;

    /* sync the parser's cur_tok to the asm parser's position */
    cur_tok = *tok_ptr;
    pushback_tok = NULL;
    ok = 0;

    /* expect '(' */
    if (cur_tok->kind != TOK_LPAREN) {
        goto out;
    }
    advance(); /* consume '(' */

    /* parse the expression using the full parser */
    n = parse_expr();
    if (n == NULL) {
        /* sync back */
        *tok_ptr = cur_tok;
        goto out;
    }

    /* add type info for sizeof resolution etc. */
    add_type(n);
    n = constant_fold(n);

    /* expect ')' */
    if (cur_tok->kind == TOK_RPAREN) {
        advance(); /* consume ')' */
    }

    /* sync back to the asm parser */
    *tok_ptr = cur_tok;

    /* try to extract constant value */
    if (n->kind == ND_NUM) {
        *out_val = n->val;
        ok = 1;
        goto out;
    }

    /* try to evaluate cast of constant */
    if (n->kind == ND_CAST && n->lhs && n->lhs->kind == ND_NUM) {
        *out_val = n->lhs->val;
        ok = 1;
        goto out;
    }

out:
    /* Leave the parser with no pending lookahead; the caller updates
     * its own token stream via *tok_ptr. */
    pushback_tok = NULL;
    return ok;
}

/*
 * attr_const_expr_eval - callback for ext_attrs.c to evaluate constant
 * expressions in attribute arguments like __aligned__(sizeof(int)).
 * The tok_ptr points at the '(' token. Consumes through ')'.
 * Returns 1 on success with value in *out_val, 0 on failure.
 */
static int attr_const_expr_eval(struct tok **tok_ptr, long *out_val)
{
    struct node *n;
    int ok;

    /* sync the parser's cur_tok to the attribute parser's position */
    cur_tok = *tok_ptr;
    pushback_tok = NULL;
    ok = 0;

    /* expect '(' */
    if (cur_tok->kind != TOK_LPAREN) {
        goto out;
    }
    advance(); /* consume '(' */

    /* parse the expression using the full parser */
    n = parse_expr();
    if (n == NULL) {
        *tok_ptr = cur_tok;
        goto out;
    }

    /* add type info for sizeof resolution etc. */
    add_type(n);

    /* expect ')' */
    if (cur_tok->kind == TOK_RPAREN) {
        advance(); /* consume ')' */
    }

    /* sync back to the attribute parser */
    *tok_ptr = cur_tok;

    /* try to extract constant value */
    if (n->kind == ND_NUM) {
        *out_val = n->val;
        ok = 1;
        goto out;
    }

    /* try to evaluate cast of constant */
    if (n->kind == ND_CAST && n->lhs && n->lhs->kind == ND_NUM) {
        *out_val = n->lhs->val;
        ok = 1;
        goto out;
    }

out:
    /* Leave the parser with no pending lookahead; the caller updates
     * its own token stream via *tok_ptr. */
    pushback_tok = NULL;
    return ok;
}

static struct symbol *add_local(const char *name, struct type *ty)
{
    struct symbol *sym;

    sym = (struct symbol *)arena_alloc(parse_arena, sizeof(struct symbol));
    memset(sym, 0, sizeof(struct symbol));
    sym->name = str_dup(parse_arena, name, (int)strlen(name));
    sym->ty = ty;
    sym->is_local = 1;
    sym->is_defined = 1;

    /* assign stack offset.
     * Small structs (1-3 bytes) passed in registers need at
     * least 4 bytes of stack space so 32-bit register stores
     * don't overwrite adjacent variables. */
    {
        int tsz;
        tsz = type_size(ty);
        if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) &&
            tsz < 4) {
            tsz = 4;
        }
        local_offset += tsz;
    }
    local_offset = (local_offset + type_align(ty) - 1) & ~(type_align(ty) - 1);
    /* ensure non-zero offset so codegen distinguishes locals from globals */
    if (local_offset == 0) {
        local_offset = 1;
    }
    sym->offset = local_offset;

    sym->next = cur_scope->locals;
    cur_scope->locals = sym;
    return sym;
}

static struct symbol *add_global(const char *name, struct type *ty)
{
    struct symbol *sym;

    sym = find_global(name);
    if (sym != NULL) {
        /* forward declaration or redefinition */
        sym->ty = ty;
        return sym;
    }

    sym = (struct symbol *)arena_alloc(parse_arena, sizeof(struct symbol));
    memset(sym, 0, sizeof(struct symbol));
    sym->name = str_dup(parse_arena, name, (int)strlen(name));
    sym->ty = ty;
    sym->is_local = 0;
    sym->is_defined = 0;

    sym->next = globals;
    globals = sym;
    return sym;
}

/* ---- typedef ---- */

static int is_typename(const char *name)
{
    int i;

    for (i = ntypedefs - 1; i >= 0; i--) {
        if (strcmp(typedefs[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static struct type *find_typedef(const char *name)
{
    struct scope *s;
    int i;

    for (s = cur_scope; s != NULL; s = s->parent) {
        for (i = ntypedefs - 1; i >= 0; i--) {
            if (typedefs[i].scope == s &&
                strcmp(typedefs[i].name, name) == 0) {
                return typedefs[i].ty;
            }
        }
    }
    return NULL;
}

static void add_typedef(const char *name, struct type *ty)
{
    if (ntypedefs >= MAX_TYPEDEFS) {
        err("too many typedefs");
        return;
    }
    typedefs[ntypedefs].name = str_dup(parse_arena, name, (int)strlen(name));
    typedefs[ntypedefs].ty = ty;
    typedefs[ntypedefs].scope = cur_scope;
    typedefs[ntypedefs].scope_depth = scope_depth;
    ntypedefs++;
}

/* ---- tags (struct/union/enum) ---- */

static struct tag_entry *find_tag_entry(const char *name)
{
    struct scope *s;
    int i;

    for (s = cur_scope; s != NULL; s = s->parent) {
        for (i = ntags - 1; i >= 0; i--) {
            if (tags[i].scope == s &&
                strcmp(tags[i].name, name) == 0) {
                return &tags[i];
            }
        }
    }
    return NULL;
}

static void add_tag(const char *name, struct type *ty)
{
    if (ntags >= MAX_TAGS) {
        err("too many struct/union/enum tags");
        return;
    }
    tags[ntags].name = str_dup(parse_arena, name, (int)strlen(name));
    tags[ntags].ty = ty;
    tags[ntags].scope = cur_scope;
    tags[ntags].scope_depth = scope_depth;
    ntags++;
}

/* ---- enum values ---- */

static int find_enum_val(const char *name, long *val)
{
    int i;

    for (i = nenum_vals - 1; i >= 0; i--) {
        if (strcmp(enum_vals[i].name, name) == 0) {
            if (val) *val = enum_vals[i].val;
            return 1;
        }
    }
    return 0;
}

static void add_enum_val(const char *name, long val)
{
    if (nenum_vals >= MAX_ENUM_VALS) {
        err("too many enum constants");
        return;
    }
    enum_vals[nenum_vals].name = str_dup(parse_arena, name, (int)strlen(name));
    enum_vals[nenum_vals].val = val;
    nenum_vals++;
}

/* ---- forward declarations for recursive descent ---- */
static struct type *parse_declspec(int *is_typedef_out, int *is_extern,
                                    int *is_static);
static struct type *parse_declarator(struct type *base, char **name_out);
static struct type *parse_type_name(void);
static struct node *parse_stmt(void);
static struct node *parse_compound_stmt(void);
static struct node *parse_expr(void);
static struct node *parse_assign(void);
static struct node *parse_ternary(void);
static struct node *parse_unary(void);
static struct node *parse_global_init(struct type *ty);

/* ---- type checking helpers ---- */

static void add_type(struct node *n);

/*
 * promote_bitfield_type - apply integer promotion rules for bitfield
 * operands.  A bitfield whose values all fit in 'int' promotes to
 * 'int'; otherwise to 'unsigned int'.
 */
static struct type *promote_bitfield_type(struct node *n)
{
    if (n == NULL || n->ty == NULL) return n ? n->ty : NULL;
    if (n->kind == ND_MEMBER && n->bit_width > 0) {
        /* unsigned bitfield fits in signed int if width < 32 */
        if (n->ty->is_unsigned && n->bit_width < 32) {
            return ty_int;
        }
        /* signed bitfield fits in signed int if width <= 32 */
        if (!n->ty->is_unsigned && n->bit_width <= 32) {
            return ty_int;
        }
    }
    return n->ty;
}

static struct type *get_common_type(struct node *lhs, struct node *rhs)
{
    struct type *lt, *rt;
    if (lhs->ty == NULL || rhs->ty == NULL) {
        return ty_int;
    }
    lt = promote_bitfield_type(lhs);
    rt = promote_bitfield_type(rhs);
    return type_common(lt, rt);
}

static void add_type(struct node *n)
{
    if (n == NULL || n->ty != NULL) {
        return;
    }

    add_type(n->lhs);
    add_type(n->rhs);
    add_type(n->cond);
    add_type(n->then_);
    add_type(n->els);

    switch (n->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
        /* pointer arithmetic */
        if (n->kind == ND_ADD || n->kind == ND_SUB) {
            if (n->lhs && type_is_pointer(n->lhs->ty)) {
                n->ty = n->lhs->ty;
                return;
            }
            if (n->rhs && type_is_pointer(n->rhs->ty)) {
                n->ty = n->rhs->ty;
                return;
            }
        }
        n->ty = get_common_type(n->lhs, n->rhs);
        return;
    case ND_BITAND:
    case ND_BITOR:
    case ND_BITXOR:
        n->ty = get_common_type(n->lhs, n->rhs);
        return;
    case ND_SHL:
    case ND_SHR:
        /* C89 6.3.7: integer promotions on left operand;
         * result type is that of the promoted left operand.
         * Types smaller than int promote to int. */
        if (n->lhs && n->lhs->ty && n->lhs->ty->size >= 4) {
            n->ty = n->lhs->ty;
        } else {
            n->ty = ty_int;
        }
        return;
    case ND_BITNOT:
        /* integer promotions apply to unary ~ operand */
        if (n->lhs && n->lhs->ty && n->lhs->ty->size >= 4) {
            n->ty = n->lhs->ty;
        } else {
            n->ty = ty_int;
        }
        return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_LOGAND:
    case ND_LOGOR:
    case ND_LOGNOT:
        n->ty = ty_int;
        return;
    case ND_ASSIGN:
        n->ty = n->lhs ? n->lhs->ty : ty_int;
        return;
    case ND_ADDR:
        if (n->lhs && n->lhs->ty) {
            n->ty = type_ptr(n->lhs->ty);
        } else {
            n->ty = type_ptr(ty_void);
        }
        return;
    case ND_DEREF:
        if (n->lhs && n->lhs->ty && n->lhs->ty->base) {
            n->ty = n->lhs->ty->base;
        } else {
            n->ty = ty_int;
        }
        return;
    case ND_MEMBER:
        /* type set during parsing */
        return;
    case ND_TERNARY:
        if (n->then_ != NULL && n->els != NULL &&
            n->then_->ty != NULL && n->els->ty != NULL) {
            /* Apply usual arithmetic conversions between branches.
             * For struct/union/pointer types, keep the then type. */
            if ((n->then_->ty->kind == TY_STRUCT ||
                 n->then_->ty->kind == TY_UNION) ||
                type_is_pointer(n->then_->ty)) {
                n->ty = n->then_->ty;
            } else {
                n->ty = get_common_type(n->then_, n->els);
            }
        } else {
            n->ty = n->then_ ? n->then_->ty : ty_int;
        }
        return;
    case ND_COMMA_EXPR:
        {
            struct type *rty;

            /* Comma operator yields the right operand after lvalue
             * conversion (arrays/functions decay to pointers). */
            rty = n->rhs ? n->rhs->ty : ty_int;
            if (rty != NULL && rty->kind == TY_ARRAY &&
                rty->base != NULL) {
                rty = type_ptr(rty->base);
            } else if (rty != NULL && rty->kind == TY_FUNC) {
                rty = type_ptr(rty);
            }
            n->ty = rty ? rty : ty_int;
        }
        return;
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
        n->ty = n->lhs ? n->lhs->ty : ty_int;
        return;
    case ND_NUM:
        if (n->ty == NULL) {
            n->ty = ty_int;
        }
        return;
    case ND_CAST:
        /* ty already set */
        return;
    case ND_CALL:
        if (n->ty == NULL) {
            n->ty = ty_int;
        }
        return;
    case ND_STMT_EXPR:
        /* type already set during parsing */
        return;
    case ND_GCC_ASM:
        /* Inline asm is a statement, not an expression. */
        n->ty = ty_void;
        return;
    case ND_LABEL_ADDR:
        /* type (void*) set during parsing */
        return;
    case ND_BUILTIN_OVERFLOW:
        /* type (int) set during parsing */
        return;
    default:
        return;
    }
}

/* ---- check if current token starts a type name ---- */

static int is_type_token(struct tok *t)
{
    switch (t->kind) {
    /* C89 type tokens */
    case TOK_VOID:
    case TOK_CHAR_KW:
    case TOK_SHORT:
    case TOK_INT:
    case TOK_LONG:
    case TOK_FLOAT:
    case TOK_DOUBLE:
    case TOK_SIGNED:
    case TOK_UNSIGNED:
    case TOK_STRUCT:
    case TOK_UNION:
    case TOK_ENUM:
    case TOK_CONST:
    case TOK_VOLATILE:
    case TOK_STATIC:
    case TOK_EXTERN:
    case TOK_AUTO:
    case TOK_REGISTER:
    case TOK_TYPEDEF:
        return 1;
    /* C99 type tokens */
    case TOK_BOOL:
        return cc_has_feat(FEAT_BOOL);
    case TOK_COMPLEX:
        return cc_has_feat2(FEAT2_COMPLEX);
    case TOK_RESTRICT:
        return cc_has_feat(FEAT_RESTRICT);
    case TOK_INLINE:
        return cc_has_feat(FEAT_INLINE);
    /* C11 type tokens */
    case TOK_ALIGNAS:
        return cc_has_feat(FEAT_ALIGNAS);
    case TOK_NORETURN:
        return cc_has_feat(FEAT_NORETURN);
    case TOK_ATOMIC:
        return cc_has_feat(FEAT_ATOMIC);
    case TOK_THREAD_LOCAL:
        return cc_has_feat(FEAT_THREAD_LOCAL);
    /* C23 type tokens */
    case TOK_BOOL_KW:
        return cc_has_feat(FEAT_BOOL_KW);
    case TOK_TYPEOF:
    case TOK_TYPEOF_UNQUAL:
        return cc_has_feat(FEAT_TYPEOF);
    case TOK_CONSTEXPR:
        return cc_has_feat2(FEAT2_CONSTEXPR);
    case TOK_IDENT:
        if (t->str && is_typename(t->str)) {
            return 1;
        }
        if (t->str && strcmp(t->str, "__builtin_va_list") == 0) {
            return 1;
        }
        /* GCC __int128 / __uint128_t / __int128_t */
        if (t->str && (strcmp(t->str, "__int128") == 0 ||
            strcmp(t->str, "__uint128_t") == 0 ||
            strcmp(t->str, "__int128_t") == 0)) {
            return 1;
        }
        /* GNU __typeof__ / __typeof / typeof are type specifiers */
        if (t->str && attr_is_typeof_keyword(t)) {
            return 1;
        }
        /* GNU __extension__ before a declaration */
        if (t->str && attr_is_extension_keyword(t)) {
            return 1;
        }
        /* GNU __attribute__ on declarations */
        if (t->str && attr_is_attribute_keyword(t)) {
            return 1;
        }
        /* Linux kernel raw annotations can prefix a declaration or a cast
         * type name, e.g. "static __always_inline int f(...)" or
         * "(__force long)x". Treat them as declaration-start tokens so
         * parse_stmt routes into parse_declaration/parse_type_name. */
        if (t->str && is_kernel_annot(t)) {
            return 1;
        }
        /* GNU __auto_type */
        if (t->str && attr_is_auto_type(t)) {
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

/* ---- parse struct/union ---- */

static struct type *parse_struct_union(int is_union)
{
    struct type *ty;
    struct tag_entry *tag_ent;
    char *tag_name;
    struct tok *t;
    struct type *base_ty;
    int dummy_td;
    int dummy_ext;
    int dummy_static;
    char *mname;
    struct type *mty;
    struct member *mbr;
    struct node *width_expr;
    int bw;

    tag_name = NULL;
    ty = NULL;
    tag_ent = NULL;

    /* skip __attribute__((...)) before tag name (e.g. struct __attribute__((packed)) name) */
    skip_all_attrs_and_ext();

    /* optional tag name */
    if (peek()->kind == TOK_IDENT &&
        !(peek()->str && attr_is_attribute_keyword(peek())) &&
        !(peek()->str && attr_is_extension_keyword(peek()))) {
        t = advance();
        tag_name = str_dup(parse_arena, t->str, t->len);
        tag_ent = find_tag_entry(tag_name);
        if (tag_ent != NULL) {
            ty = tag_ent->ty;
        }
    }

    /* skip __attribute__((...)) after tag name */
    skip_all_attrs_and_ext();

    if (peek()->kind == TOK_LBRACE) {
        advance();

        if (ty == NULL || tag_ent == NULL || tag_ent->scope != cur_scope) {
            ty = (struct type *)arena_alloc(parse_arena, sizeof(struct type));
            memset(ty, 0, sizeof(struct type));
            ty->kind = is_union ? TY_UNION : TY_STRUCT;
            ty->origin = ty;
            if (tag_name) {
                ty->name = tag_name;
                add_tag(tag_name, ty);
            }
        }

        /* parse members */
        while (peek()->kind != TOK_RBRACE && peek()->kind != TOK_EOF) {
            struct tok *member_start;
            member_start = peek();

            /* C11: _Static_assert inside struct/union */
            if ((peek()->kind == TOK_STATIC_ASSERT &&
                 cc_has_feat(FEAT_STATIC_ASSERT)) ||
                (peek()->kind == TOK_STATIC_ASSERT_KW &&
                 cc_has_feat2(FEAT2_STATIC_ASSERT_NS))) {
                struct tok *sa_tok;
                struct node *sa_expr;
                sa_tok = peek();
                advance();
                expect(TOK_LPAREN, "'('");
                sa_expr = parse_assign();
                add_type(sa_expr);
                if (peek()->kind == TOK_COMMA) {
                    advance();
                    /* parse message expression (handles
                     * string concatenation) */
                    parse_assign();
                }
                expect(TOK_RPAREN, "')'");
                expect(TOK_SEMI, "';'");
                if (sa_expr->kind == ND_NUM &&
                    sa_expr->val == 0) {
                    diag_warn(sa_tok->file, sa_tok->line,
                              sa_tok->col,
                              "static assertion failed (warning)");
                }
                continue;
            }
            base_ty = parse_declspec(&dummy_td, &dummy_ext, &dummy_static);

            /* handle unnamed bitfield: type : width ; */
            if (peek()->kind == TOK_COLON) {
                advance(); /* consume ':' */
                width_expr = parse_assign();
                bw = 0;
                if (width_expr && width_expr->kind == ND_NUM) {
                    bw = (int)width_expr->val;
                }
                mbr = type_add_member(ty, NULL, base_ty);
                mbr->bit_width = bw;
                expect(TOK_SEMI, "';'");
                continue;
            }

            /* anonymous struct/union member: if the base type
             * is a struct/union and next token is ';', add an
             * unnamed member so its fields are accessible */
            if (peek()->kind == TOK_SEMI &&
                (base_ty->kind == TY_STRUCT ||
                 base_ty->kind == TY_UNION)) {
                type_add_member(ty, NULL, base_ty);
                expect(TOK_SEMI, "';'");
                continue;
            }

            while (peek()->kind != TOK_SEMI && peek()->kind != TOK_EOF) {
                mname = NULL;
                mty = parse_declarator(base_ty, &mname);

                /* parse bitfield width ': N' */
                bw = 0;
                if (peek()->kind == TOK_COLON) {
                    advance(); /* consume ':' */
                    width_expr = parse_assign();
                    if (width_expr && width_expr->kind == ND_NUM) {
                        bw = (int)width_expr->val;
                    }
                    /* skip trailing __attribute__((packed)) etc. */
                    skip_all_attrs_and_ext();
                }

                mbr = type_add_member(ty, mname, mty);
                mbr->bit_width = bw;

                if (peek()->kind != TOK_COMMA) {
                    break;
                }
                advance();
            }
            expect(TOK_SEMI, "';'");

            /* Error recovery: if no tokens were consumed during this
             * member parse iteration, skip forward to the next ';'
             * or '}' to avoid infinite error loops. */
            if (peek() == member_start) {
                while (peek()->kind != TOK_SEMI &&
                       peek()->kind != TOK_RBRACE &&
                       peek()->kind != TOK_EOF) {
                    advance();
                }
                if (peek()->kind == TOK_SEMI) advance();
            }
        }
        expect(TOK_RBRACE, "'}'");

        /* parse trailing __attribute__((...)) on struct/union
         * (e.g. struct x { ... } __attribute__((aligned(32))))
         * and apply to the type after layout. */
        {
            struct attr_info su_attrs;
            attr_info_init(&su_attrs);
            while (peek()->kind == TOK_IDENT && peek()->str &&
                   attr_is_attribute_keyword(peek())) {
                attr_try_parse(&su_attrs, &cur_tok);
            }
            skip_all_attrs_and_ext();

            /* compute layout */
            if (is_union) {
                type_complete_union(ty);
            } else {
                type_complete_struct(ty);
            }

            /* apply attributes (e.g. aligned) after layout */
            attr_apply_to_type(ty, &su_attrs);
        }
    } else if (ty == NULL) {
        /* forward declaration */
        ty = (struct type *)arena_alloc(parse_arena, sizeof(struct type));
        memset(ty, 0, sizeof(struct type));
        ty->kind = is_union ? TY_UNION : TY_STRUCT;
        ty->origin = ty;
        if (tag_name) {
            ty->name = tag_name;
            add_tag(tag_name, ty);
        }
    }

    return ty;
}

/* ---- parse enum ---- */

static struct type *parse_enum_spec(void)
{
    struct type *ty;
    struct tag_entry *tag_ent;
    char *tag_name;
    struct tok *t;
    long val;
    struct node *expr;

    tag_name = NULL;
    ty = NULL;
    tag_ent = NULL;

    if (peek()->kind == TOK_IDENT) {
        t = advance();
        tag_name = str_dup(parse_arena, t->str, t->len);
        tag_ent = find_tag_entry(tag_name);
        if (tag_ent != NULL) {
            ty = tag_ent->ty;
        }
    }

    if (peek()->kind == TOK_LBRACE) {
        advance();

        if (ty == NULL || tag_ent == NULL || tag_ent->scope != cur_scope) {
            ty = type_enum();
            if (tag_name) {
                ty->name = tag_name;
                add_tag(tag_name, ty);
            }
        }

        val = 0;
        while (peek()->kind != TOK_RBRACE && peek()->kind != TOK_EOF) {
            t = expect(TOK_IDENT, "enum constant name");

            if (peek()->kind == TOK_ASSIGN) {
                advance();
                expr = parse_assign();
                add_type(expr);
                if (expr->kind == ND_NUM) {
                    val = expr->val;
                }
            }

            add_enum_val(t->str, val);
            val++;

            if (peek()->kind != TOK_COMMA) {
                break;
            }
            advance(); /* consume ',' */
        }
        expect(TOK_RBRACE, "'}'");
    } else if (ty == NULL) {
        /* forward reference to unknown enum, default to int */
        ty = type_enum();
        if (tag_name) {
            ty->name = tag_name;
            add_tag(tag_name, ty);
        }
    }

    return ty;
}

/* ---- aarch64 va_list struct type ---- */

/*
 * va_list_type - return the aarch64 va_list struct type.
 *
 * On aarch64 (AAPCS64), va_list is a struct:
 *   struct __va_list {
 *       void *__stack;     offset  0  (next stack arg)
 *       void *__gr_top;    offset  8  (end of GP save area)
 *       void *__vr_top;    offset 16  (end of FP save area)
 *       int   __gr_offs;   offset 24  (offset from gr_top to next GP)
 *       int   __vr_offs;   offset 28  (offset from vr_top to next FP)
 *   };
 *   Total size = 32, alignment = 8.
 */
/* Module-level cached va_list type, reset by parse() for each TU */
static struct type *va_list_cached;

static struct type *va_list_type(void)
{
    struct type *ty;

    if (va_list_cached) {
        return va_list_cached;
    }

    ty = (struct type *)arena_alloc(parse_arena, sizeof(struct type));
    memset(ty, 0, sizeof(struct type));
    ty->kind = TY_STRUCT;
    ty->name = "__va_list";
    ty->origin = ty;

    /* build members: 3 pointers + 2 ints */
    type_add_member(ty, "__stack",   type_ptr(ty_void));
    type_add_member(ty, "__gr_top",  type_ptr(ty_void));
    type_add_member(ty, "__vr_top",  type_ptr(ty_void));
    type_add_member(ty, "__gr_offs", ty_int);
    type_add_member(ty, "__vr_offs", ty_int);

    type_complete_struct(ty);
    /* size=32 align=8 after layout */

    va_list_cached = ty;
    return ty;
}

/* ---- parse declaration specifiers ---- */

static struct type *parse_declspec(int *is_typedef_out, int *is_extern,
                                    int *is_static)
{
    struct type *ty;
    struct tok *t;
    int has_signed;
    int has_unsigned;
    int has_short;
    int has_long;
    int long_count;
    int has_void;
    int has_char;
    int has_int;
    int has_float;
    int has_double;
    int has_complex;
    int has_type;
    int q_const;
    int q_volatile;
    int q_restrict;
    int q_atomic;

    ty = NULL;
    has_signed = 0;
    has_unsigned = 0;
    has_short = 0;
    has_long = 0;
    long_count = 0;
    has_void = 0;
    has_char = 0;
    has_int = 0;
    has_float = 0;
    has_double = 0;
    has_complex = 0;
    has_type = 0;
    q_const = 0;
    q_volatile = 0;
    q_restrict = 0;
    q_atomic = 0;

    if (is_typedef_out) *is_typedef_out = 0;
    if (is_extern) *is_extern = 0;
    if (is_static) *is_static = 0;

    for (;;) {
        t = peek();

        /* Linux kernel raw annotation identifiers (ignore). */
        if (is_kernel_annot(t)) {
            skip_kernel_annot();
            continue;
        }

        /* storage class specifiers */
        if (t->kind == TOK_TYPEDEF) {
            advance();
            if (is_typedef_out) *is_typedef_out = 1;
            continue;
        }
        if (t->kind == TOK_EXTERN) {
            advance();
            if (is_extern) *is_extern = 1;
            continue;
        }
        if (t->kind == TOK_STATIC) {
            advance();
            if (is_static) *is_static = 1;
            continue;
        }
        if (t->kind == TOK_AUTO || t->kind == TOK_REGISTER) {
            advance();
            continue;
        }

        /* C11: _Thread_local storage class */
        if (t->kind == TOK_THREAD_LOCAL &&
            cc_has_feat(FEAT_THREAD_LOCAL)) {
            advance();
            continue;
        }

        /* type qualifiers */
        if (t->kind == TOK_CONST || t->kind == TOK_VOLATILE) {
            if (t->kind == TOK_CONST) {
                q_const = 1;
            } else {
                q_volatile = 1;
            }
            advance();
            continue;
        }
        /* C99: restrict qualifier */
        if (t->kind == TOK_RESTRICT && cc_has_feat(FEAT_RESTRICT)) {
            q_restrict = 1;
            advance();
            continue;
        }
        /* C11: _Atomic qualifier */
        if (t->kind == TOK_ATOMIC && cc_has_feat(FEAT_ATOMIC)) {
            q_atomic = 1;
            advance();
            continue;
        }

        /* C99: inline function specifier */
        if (t->kind == TOK_INLINE && cc_has_feat(FEAT_INLINE)) {
            advance();
            continue;
        }
        /* C11: _Noreturn function specifier */
        if (t->kind == TOK_NORETURN && cc_has_feat(FEAT_NORETURN)) {
            advance();
            continue;
        }
        /* C23: constexpr */
        if (t->kind == TOK_CONSTEXPR &&
            cc_has_feat2(FEAT2_CONSTEXPR)) {
            advance();
            continue;
        }

        /* C23: [[ attribute ]] -- skip attributes on declarations */
        if (t->kind == TOK_LBRACKET) {
            if (skip_c23_attr()) continue;
            break;
        }
        if (t->kind == TOK_ATTR_OPEN &&
            cc_has_feat(FEAT_ATTR_SYNTAX)) {
            advance(); /* consume [[ */
            while (peek()->kind != TOK_ATTR_CLOSE &&
                   peek()->kind != TOK_EOF) {
                advance();
            }
            if (peek()->kind == TOK_ATTR_CLOSE) {
                advance(); /* consume ]] */
            }
            continue;
        }

        /* GNU __attribute__((...)) on declaration specifiers */
        if (t->kind == TOK_IDENT && t->str &&
            attr_is_attribute_keyword(t)) {
            if (declspec_attrs) {
                attr_try_parse(declspec_attrs, &cur_tok);
            } else {
                skip_attribute();
            }
            continue;
        }

        /* GNU __extension__ - just skip it */
        if (t->kind == TOK_IDENT && t->str &&
            attr_is_extension_keyword(t)) {
            advance();
            continue;
        }

        /* GNU __typeof__ / __typeof as type specifier */
        if (t->kind == TOK_IDENT && t->str &&
            attr_is_typeof_keyword(t) && !has_type) {
            advance();
            expect(TOK_LPAREN, "'('");
            if (is_type_token(peek())) {
                ty = parse_type_name();
            } else {
                struct node *te;
                te = parse_assign();
                add_type(te);
                ty = te->ty;
            }
            expect(TOK_RPAREN, "')'");
            if (ty == NULL) {
                ty = ty_int;
            }
            has_type = 1;
            break;
        }

        /* GNU __auto_type - infer from initializer */
        if (t->kind == TOK_IDENT && t->str &&
            attr_is_auto_type(t) && !has_type) {
            advance();
            ty = &ty_auto_type_sentinel;
            has_type = 1;
            break;
        }

        /* GNU _Noreturn / __noreturn as identifier */
        if (t->kind == TOK_IDENT && t->str &&
            attr_is_noreturn_keyword(t)) {
            advance();
            continue;
        }

        /* type specifiers */
        if (t->kind == TOK_VOID) {
            advance();
            has_void = 1;
            has_type = 1;
            continue;
        }
        if (t->kind == TOK_CHAR_KW) {
            advance();
            has_char = 1;
            has_type = 1;
            continue;
        }
        if (t->kind == TOK_SHORT) {
            advance();
            has_short = 1;
            has_type = 1;
            continue;
        }
        if (t->kind == TOK_INT) {
            advance();
            has_type = 1;
            continue;
        }
        if (t->kind == TOK_LONG) {
            advance();
            has_long = 1;
            long_count++;
            has_type = 1;
            continue;
        }
        if (t->kind == TOK_FLOAT) {
            advance();
            has_float = 1;
            has_type = 1;
            continue;
        }
        if (t->kind == TOK_DOUBLE) {
            advance();
            has_double = 1;
            has_type = 1;
            continue;
        }
        /* C99: _Complex type specifier */
        if (t->kind == TOK_COMPLEX && cc_has_feat2(FEAT2_COMPLEX)) {
            advance();
            has_complex = 1;
            has_type = 1;
            continue;
        }
        if (t->kind == TOK_SIGNED) {
            advance();
            has_signed = 1;
            has_type = 1;
            continue;
        }
        if (t->kind == TOK_UNSIGNED) {
            advance();
            has_unsigned = 1;
            has_type = 1;
            continue;
        }

        /* C99: _Bool type */
        if ((t->kind == TOK_BOOL && cc_has_feat(FEAT_BOOL)) ||
            (t->kind == TOK_BOOL_KW && cc_has_feat(FEAT_BOOL_KW))) {
            advance();
            ty = ty_bool;
            has_type = 1;
            break;
        }

        /* C23: typeof / typeof_unqual */
        if ((t->kind == TOK_TYPEOF || t->kind == TOK_TYPEOF_UNQUAL) &&
            cc_has_feat(FEAT_TYPEOF)) {
            advance();
            expect(TOK_LPAREN, "'('");
            if (is_type_token(peek())) {
                ty = parse_type_name();
            } else {
                struct node *te;
                te = parse_assign();
                add_type(te);
                ty = te->ty;
            }
            expect(TOK_RPAREN, "')'");
            if (ty == NULL) {
                ty = ty_int;
            }
            has_type = 1;
            break;
        }

        /* C11: _Alignas */
        if (t->kind == TOK_ALIGNAS && cc_has_feat(FEAT_ALIGNAS)) {
            advance();
            /* consume _Alignas(expr) or _Alignas(type) */
            expect(TOK_LPAREN, "'('");
            if (is_type_token(peek())) {
                parse_type_name();
            } else {
                parse_assign();
            }
            expect(TOK_RPAREN, "')'");
            continue;
        }

        /* struct/union - continue to consume trailing const/volatile */
        if (t->kind == TOK_STRUCT) {
            advance();
            ty = parse_struct_union(0);
            has_type = 1;
            continue;
        }
        if (t->kind == TOK_UNION) {
            advance();
            ty = parse_struct_union(1);
            has_type = 1;
            continue;
        }

        /* enum - continue to consume trailing const/volatile */
        if (t->kind == TOK_ENUM) {
            advance();
            ty = parse_enum_spec();
            has_type = 1;
            continue;
        }

        /* __builtin_va_list -- aarch64 va_list struct */
        if (t->kind == TOK_IDENT && !has_type && t->str &&
            strcmp(t->str, "__builtin_va_list") == 0) {
            ty = va_list_type();
            advance();
            has_type = 1;
            break;
        }

        /* GCC __int128 extension - handle like a base type specifier
         * so it combines with signed/unsigned. */
        if (t->kind == TOK_IDENT && t->str &&
            strcmp(t->str, "__int128") == 0) {
            ty = has_unsigned ? ty_uint128 : ty_int128;
            advance();
            has_type = 1;
            break;
        }

        /* GCC __uint128_t / __int128_t builtins */
        if (t->kind == TOK_IDENT && t->str &&
            strcmp(t->str, "__uint128_t") == 0) {
            ty = ty_uint128;
            advance();
            has_type = 1;
            break;
        }
        if (t->kind == TOK_IDENT && t->str &&
            strcmp(t->str, "__int128_t") == 0) {
            ty = ty_int128;
            advance();
            has_type = 1;
            break;
        }

        /* typedef name */
        if (t->kind == TOK_IDENT && !has_type && is_typename(t->str)) {
            ty = find_typedef(t->str);
            advance();
            has_type = 1;
            /* continue to consume trailing const/volatile qualifiers
             * (e.g. "VdbeOpList const *aOp") */
            continue;
        }

        break; /* not a declspec token */
    }

    if (ty == NULL) {
        /* build type from specifiers */
        if (has_void) {
            ty = ty_void;
        } else if (has_char) {
            if (has_complex) {
                ty = ty_cfloat; /* _Complex char -> treat as _Complex float */
            } else if (has_unsigned) {
                ty = ty_uchar;
            } else if (has_signed) {
                ty = ty_schar;
            } else {
                ty = ty_char;  /* plain char: unsigned on aarch64 */
            }
        } else if (has_short) {
            if (has_complex) {
                ty = ty_cfloat; /* _Complex short -> treat as _Complex float */
            } else {
                ty = has_unsigned ? ty_ushort : ty_short;
            }
        } else if (has_long && long_count >= 2 &&
                   cc_has_feat(FEAT_LONG_LONG)) {
            if (has_complex) {
                ty = ty_cdouble;
            } else {
                ty = has_unsigned ? ty_ullong : ty_llong;
            }
        } else if (has_float) {
            if (has_complex) {
                ty = ty_cfloat;
            } else {
                ty = ty_float;
            }
        } else if (has_double) {
            if (has_complex) {
                ty = ty_cdouble;
            } else {
                ty = (has_long && ty_ldouble != NULL) ? ty_ldouble : ty_double;
            }
        } else if (has_long) {
            if (has_complex) {
                ty = ty_cdouble; /* _Complex long -> _Complex double */
            } else {
                ty = has_unsigned ? ty_ulong : ty_long;
            }
        } else if (has_complex) {
            /* _Complex with no float/double defaults to _Complex double */
            ty = ty_cdouble;
        } else if (has_unsigned) {
            ty = ty_uint;
        } else {
            (void)has_signed;
            (void)has_int;
            (void)long_count;
            /* implicit int (C89 allows it) */
            ty = ty_int;
        }
    }

    return qualify_type(ty, q_const, q_volatile, q_restrict, q_atomic);
}

/* ---- parse declarator ---- */

/*
 * declarator = "*"* ( "(" declarator ")" | ident ) type-suffix
 * type-suffix = "[" num "]" type-suffix
 *             | "(" param-list ")" type-suffix
 *             | empty
 */

static struct type *parse_type_suffix(struct type *ty);
static struct type *parse_params(struct type *ret);

/*
 * fixup_placeholder_sizes - after filling a placeholder with its real type,
 * walk from ty down to placeholder and recompute sizes bottom-up.
 * This fixes array types built on a zero-sized placeholder.
 */
static void fixup_placeholder_sizes(struct type *ty)
{
    if (ty == NULL || ty->base == NULL) {
        return;
    }
    /* recurse first so inner types are fixed before outer */
    fixup_placeholder_sizes(ty->base);
    if (ty->kind == TY_ARRAY) {
        ty->size = ty->base->size * ty->array_len;
        ty->align = ty->base->align;
    } else if (ty->kind == TY_PTR) {
        /* pointer size/align is always 8, no fixup needed */
    }
}

/*
 * copy_type - make a shallow arena copy of a type so we can set
 * name/next without corrupting shared global types like ty_int.
 */
static struct type *copy_type(struct type *src)
{
    struct type *dst;

    dst = (struct type *)arena_alloc(parse_arena, sizeof(struct type));
    memcpy(dst, src, sizeof(struct type));
    return dst;
}

static struct type *qualify_type(struct type *ty, int q_const,
                                 int q_volatile, int q_restrict,
                                 int q_atomic)
{
    struct type *copy;

    if (ty == NULL) {
        return NULL;
    }
    if (!q_const && !q_volatile && !q_restrict && !q_atomic) {
        return ty;
    }

    copy = copy_type(ty);
    if (copy->kind == TY_ARRAY) {
        if (copy->base != NULL) {
            copy->base = qualify_type(copy->base, q_const, q_volatile,
                                      q_restrict, q_atomic);
        }
        return copy;
    }

    if (q_const) {
        copy->is_const = 1;
    }
    if (q_volatile) {
        copy->is_volatile = 1;
    }
    if (q_restrict) {
        copy->is_restrict = 1;
    }
    if (q_atomic) {
        copy->is_atomic = 1;
    }
    return copy;
}

static struct type *parse_declarator(struct type *base, char **name_out)
{
    struct type *ty;
    struct tok *t;
    struct type *inner_ty;
    char *inner_name;

    /* Arrays can be resized later when length inference runs.
     * Snapshot them per declarator so sibling declarators do not
     * share the same type object. */
    if (base != NULL && base->kind == TY_ARRAY) {
        base = copy_type(base);
    }

    /* pointer(s) */
    while (peek()->kind == TOK_STAR) {
        int q_const;
        int q_volatile;
        int q_restrict;
        int q_atomic;

        advance();
        base = type_ptr(base);
        q_const = 0;
        q_volatile = 0;
        q_restrict = 0;
        q_atomic = 0;
        /* capture const/volatile/restrict/_Atomic after * */
        while (peek()->kind == TOK_CONST ||
               peek()->kind == TOK_VOLATILE ||
               (peek()->kind == TOK_RESTRICT &&
                cc_has_feat(FEAT_RESTRICT)) ||
               (peek()->kind == TOK_ATOMIC &&
                cc_has_feat(FEAT_ATOMIC))) {
            if (peek()->kind == TOK_CONST) {
                q_const = 1;
            } else if (peek()->kind == TOK_VOLATILE) {
                q_volatile = 1;
            } else if (peek()->kind == TOK_RESTRICT &&
                       cc_has_feat(FEAT_RESTRICT)) {
                q_restrict = 1;
            } else if (peek()->kind == TOK_ATOMIC &&
                       cc_has_feat(FEAT_ATOMIC)) {
                q_atomic = 1;
            }
            advance();
        }
        /* Kernel annotations can appear between pointer stars, e.g.:
         *   struct foo * __percpu *bar;
         * Skip them without breaking the '*' chain. */
        while (is_kernel_annot(peek())) {
            skip_kernel_annot();
        }
        if (q_const) {
            base->is_const = 1;
        }
        if (q_volatile) {
            base->is_volatile = 1;
        }
        if (q_restrict) {
            base->is_restrict = 1;
        }
        if (q_atomic) {
            base->is_atomic = 1;
        }
    }

    /*
     * Handle '(' -- could be:
     *   1. Grouped declarator: int (*fp)(int, int)
     *      peek after '(' is '*' or ident that is NOT a type token
     *   2. Function parameter list: int foo(int a, int b)
     *      peek after '(' is a type token or void or ')'
     *
     * For grouped declarators, we recurse. For parameter lists,
     * we fall through to parse_type_suffix.
     */
    if (peek()->kind == TOK_LPAREN) {
        /* look ahead: if next token is '*' or an ident that is not
         * a type name, this is a grouped declarator */
        struct tok *la;
        la = peek();
        (void)la;
        advance(); /* consume '(' */

        /* skip __attribute__((...)) inside grouped declarators,
         * e.g. (int(__attribute__((noinline)) *)(void)) */
        skip_all_attrs_and_ext();

        if (peek()->kind == TOK_STAR ||
            (peek()->kind == TOK_IDENT &&
             !is_type_token(peek()) && !is_typename(peek()->str))) {
            /* grouped declarator: use placeholder trick.
             * Parse inner with a placeholder, parse outer suffix
             * on the real base, then copy the suffix result into
             * the placeholder so the inner wrappers (e.g. pointer)
             * correctly wrap the outer type (e.g. function). */
            struct type *placeholder;
            placeholder = (struct type *)arena_alloc(parse_arena,
                                                     sizeof(struct type));
            memset(placeholder, 0, sizeof(struct type));
            placeholder->origin = placeholder;

            inner_name = NULL;
            inner_ty = parse_declarator(placeholder, &inner_name);
            if (name_out) {
                *name_out = inner_name;
            }
            expect(TOK_RPAREN, "')'");
            /* parse the outer type suffix using the original base */
            *placeholder = *parse_type_suffix(base);
            /* recompute sizes for any array types built on the
             * placeholder before it had its real size */
            fixup_placeholder_sizes(inner_ty);
            return inner_ty;
        }

        /* function parameter list: int foo(void) or int foo(int, int) */
        ty = parse_params(base);
        expect(TOK_RPAREN, "')'");
        if (name_out) *name_out = NULL;
        return ty;
    }

    /* skip __attribute__((...)) before identifier */
    skip_all_attrs_and_ext();

    /* identifier */
    if (peek()->kind == TOK_IDENT) {
        t = advance();
        if (name_out) {
            *name_out = str_dup(parse_arena, t->str, t->len);
        }
    } else {
        /* abstract declarator (no name) */
        if (name_out) {
            *name_out = NULL;
        }
    }

    /* GNU extension: register var asm("reg") - skip the asm part */
    if (peek()->kind == TOK_IDENT && peek()->str &&
        (strcmp(peek()->str, "asm") == 0 ||
         strcmp(peek()->str, "__asm__") == 0 ||
         strcmp(peek()->str, "__asm") == 0)) {
        advance(); /* consume asm */
        if (peek()->kind == TOK_LPAREN) {
            int asm_depth;
            advance(); /* consume ( */
            asm_depth = 1;
            while (asm_depth > 0 && peek()->kind != TOK_EOF) {
                if (peek()->kind == TOK_LPAREN) asm_depth++;
                else if (peek()->kind == TOK_RPAREN) asm_depth--;
                advance();
            }
        }
    }

    ty = parse_type_suffix(base);

    /* parse trailing __attribute__((...)) on declarators */
    attr_info_init(&declarator_attrs);
    for (;;) {
        if (attr_try_parse(&declarator_attrs, &cur_tok)) {
            continue;
        }
        /* also skip __extension__ and raw kernel annotations */
        if (peek()->kind == TOK_IDENT && peek()->str &&
            attr_is_extension_keyword(peek())) {
            advance();
            continue;
        }
        if (peek()->kind == TOK_IDENT && peek()->str &&
            is_kernel_annot(peek())) {
            skip_kernel_annot();
            continue;
        }
        break;
    }

    return ty;
}

static struct type *parse_params(struct type *ret)
{
    struct type *params;
    struct type *param_tail;
    struct type *pty;
    int dummy_td, dummy_ext, dummy_static;
    char *pname;

    params = NULL;
    param_tail = NULL;

    if (peek()->kind == TOK_VOID) {
        int q_const;
        int q_volatile;
        int q_restrict;
        int q_atomic;

        /* check for "void)" meaning no parameters */
        advance();
        if (peek()->kind == TOK_RPAREN) {
            /* void parameter list = no params */
            return type_func(ret, NULL);
        }
        /* "void" followed by more tokens means it's a type specifier
         * for the first parameter (e.g. "void *dst"). Continue parsing
         * with ty_void as the base type for the declarator.
         * Capture trailing qualifiers like "void const *p". */
        q_const = 0;
        q_volatile = 0;
        q_restrict = 0;
        q_atomic = 0;
        while (peek()->kind == TOK_CONST ||
               peek()->kind == TOK_VOLATILE ||
               (peek()->kind == TOK_RESTRICT &&
                cc_has_feat(FEAT_RESTRICT)) ||
               (peek()->kind == TOK_ATOMIC &&
                cc_has_feat(FEAT_ATOMIC))) {
            if (peek()->kind == TOK_CONST) {
                q_const = 1;
            } else if (peek()->kind == TOK_VOLATILE) {
                q_volatile = 1;
            } else if (peek()->kind == TOK_RESTRICT &&
                       cc_has_feat(FEAT_RESTRICT)) {
                q_restrict = 1;
            } else if (peek()->kind == TOK_ATOMIC &&
                       cc_has_feat(FEAT_ATOMIC)) {
                q_atomic = 1;
            }
            advance();
        }
        pname = NULL;
        pty = parse_declarator(qualify_type(ty_void, q_const, q_volatile,
                                            q_restrict, q_atomic),
                               &pname);
        if (pty->kind == TY_ARRAY) {
            pty = type_ptr(pty->base);
        }
        /* copy type to avoid mutating shared globals like ty_int */
        pty = copy_type(pty);
        if (pname) {
            pty->name = pname;
        }
        pty->next = NULL;
        params = pty;
        param_tail = pty;

        if (peek()->kind == TOK_COMMA) {
            advance();
        } else {
            return type_func(ret, params);
        }
    }

    {
        int is_var;
        struct type *fty;

        is_var = 0;
        while (peek()->kind != TOK_RPAREN && peek()->kind != TOK_EOF) {
            if (peek()->kind == TOK_ELLIPSIS) {
                advance();
                is_var = 1;
                break;
            }

            pname = NULL;
            pty = parse_declspec(&dummy_td, &dummy_ext, &dummy_static);
            pty = parse_declarator(pty, &pname);

            /* arrays decay to pointers in parameters */
            if (pty->kind == TY_ARRAY) {
                pty = type_ptr(pty->base);
            }

            /* copy type to avoid mutating shared globals like ty_int.
             * Without this, setting next on ty_int creates a circular
             * list that causes infinite loops and arena exhaustion. */
            pty = copy_type(pty);

            /* store parameter name so add_local can find it */
            if (pname) {
                pty->name = pname;
            }

            pty->next = NULL;
            if (param_tail) {
                param_tail->next = pty;
            } else {
                params = pty;
            }
            param_tail = pty;

            if (peek()->kind != TOK_COMMA) {
                break;
            }
            advance(); /* consume ',' */
        }

        fty = type_func(ret, params);
        fty->is_variadic = is_var;
        return fty;
    }
}

static struct type *parse_type_suffix(struct type *ty)
{
    struct node *expr;
    int len;

    /* array */
    if (peek()->kind == TOK_LBRACKET) {
        struct node *vla_dim;
        advance(); /* consume '[' */
        len = 0;
        expr = NULL;
        vla_dim = NULL;
        /* C99: skip 'static', 'const', 'volatile', 'restrict'
         * qualifiers in array parameter declarations
         * (e.g. int x[static 5], int x[const 5]) */
        while (peek()->kind == TOK_STATIC ||
               peek()->kind == TOK_CONST ||
               peek()->kind == TOK_VOLATILE ||
               peek()->kind == TOK_RESTRICT) {
            advance();
        }
        if (peek()->kind == TOK_STAR &&
            peek()->kind != TOK_RBRACKET) {
            /* check for [qualifier *] VLA syntax */
            struct tok *star_tok;
            star_tok = peek();
            advance(); /* consume '*' */
            if (peek()->kind == TOK_RBRACKET) {
                /* [const *] or [*] — VLA placeholder */
                len = 0;
            } else {
                /* not VLA star, put back and parse expr */
                unadvance(star_tok);
                expr = parse_assign();
                add_type(expr);
                if (expr->kind == ND_NUM) {
                    len = (int)expr->val;
                } else {
                    vla_dim = expr;
                }
            }
        } else if (peek()->kind != TOK_RBRACKET) {
            expr = parse_assign();
            add_type(expr);
            if (expr->kind == ND_NUM) {
                len = (int)expr->val;
            } else {
                vla_dim = expr;
            }
        }
        expect(TOK_RBRACKET, "']'");
        ty = parse_type_suffix(ty);
        ty = type_array(ty, len);
        /* mark VLA when dimension is a runtime expression */
        if (vla_dim != NULL) {
            ty->is_vla = 1;
            ty->vla_expr = vla_dim;
        }
        return ty;
    }

    /* function parameters */
    if (peek()->kind == TOK_LPAREN) {
        advance(); /* consume '(' */
        ty = parse_params(ty);
        expect(TOK_RPAREN, "')'");
        return ty;
    }

    return ty;
}

/* ---- parse abstract type name (for casts, sizeof) ---- */

static struct type *parse_type_name(void)
{
    struct type *base;
    int dummy;

    base = parse_declspec(&dummy, &dummy, &dummy);
    return parse_declarator(base, NULL);
}

/* ---- string literal helpers ---- */

#define STRING_LITERAL_MAX_BYTES 16384

static int string_token_width(const struct tok *t)
{
    if (t == NULL || t->kind != TOK_STR || t->raw == NULL) {
        return 1;
    }
    if (t->raw[0] == 'u' && t->raw[1] == '8' && t->raw[2] == '"') {
        return 1;
    }
    if (t->raw[0] == 'u' && t->raw[1] == '"') {
        return 2;
    }
    if ((t->raw[0] == 'U' || t->raw[0] == 'L') && t->raw[1] == '"') {
        return 4;
    }
    return 1;
}

static unsigned long decode_utf8_codepoint(const unsigned char *s, int len,
                                           int *consumed)
{
    unsigned long cp;
    unsigned char c0;
    unsigned char c1;
    unsigned char c2;
    unsigned char c3;

    c0 = s[0];
    if (c0 < 0x80 || len < 2) {
        *consumed = 1;
        return (unsigned long)c0;
    }
    if ((c0 & 0xE0) == 0xC0 && len >= 2) {
        c1 = s[1];
        if ((c1 & 0xC0) == 0x80) {
            cp = ((unsigned long)(c0 & 0x1F) << 6) |
                 (unsigned long)(c1 & 0x3F);
            *consumed = 2;
            return cp;
        }
    } else if ((c0 & 0xF0) == 0xE0 && len >= 3) {
        c1 = s[1];
        c2 = s[2];
        if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
            cp = ((unsigned long)(c0 & 0x0F) << 12) |
                 ((unsigned long)(c1 & 0x3F) << 6) |
                 (unsigned long)(c2 & 0x3F);
            *consumed = 3;
            return cp;
        }
    } else if ((c0 & 0xF8) == 0xF0 && len >= 4) {
        c1 = s[1];
        c2 = s[2];
        c3 = s[3];
        if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 &&
            (c3 & 0xC0) == 0x80) {
            cp = ((unsigned long)(c0 & 0x07) << 18) |
                 ((unsigned long)(c1 & 0x3F) << 12) |
                 ((unsigned long)(c2 & 0x3F) << 6) |
                 (unsigned long)(c3 & 0x3F);
            *consumed = 4;
            return cp;
        }
    }

    *consumed = 1;
    return (unsigned long)c0;
}

static int append_u8(char *buf, int len, int max, unsigned long v)
{
    if (len < max) {
        buf[len++] = (char)(v & 0xffUL);
    }
    return len;
}

static int append_u16(char *buf, int len, int max, unsigned long v)
{
    len = append_u8(buf, len, max, v);
    len = append_u8(buf, len, max, v >> 8);
    return len;
}

static int append_u32(char *buf, int len, int max, unsigned long v)
{
    len = append_u8(buf, len, max, v);
    len = append_u8(buf, len, max, v >> 8);
    len = append_u8(buf, len, max, v >> 16);
    len = append_u8(buf, len, max, v >> 24);
    return len;
}

static int append_wide_codepoint(char *buf, int len, int max,
                                 unsigned long cp, int width,
                                 int *units)
{
    if (width == 2) {
        if (cp <= 0xFFFFUL && (cp < 0xD800UL || cp > 0xDFFFUL)) {
            len = append_u16(buf, len, max, cp);
            *units += 1;
        } else if (cp <= 0x10FFFFUL) {
            unsigned long v;
            unsigned long hi;
            unsigned long lo;
            v = cp - 0x10000UL;
            hi = 0xD800UL + (v >> 10);
            lo = 0xDC00UL + (v & 0x3FFUL);
            len = append_u16(buf, len, max, hi);
            len = append_u16(buf, len, max, lo);
            *units += 2;
        } else {
            len = append_u16(buf, len, max, cp);
            *units += 1;
        }
    } else {
        len = append_u32(buf, len, max, cp);
        *units += 1;
    }
    return len;
}

static int string_literal_matches_type(struct type *ty, struct node *str)
{
    int str_elem_sz;

    if (ty == NULL || str == NULL || str->kind != ND_STR ||
        str->ty == NULL || str->ty->kind != TY_ARRAY ||
        str->ty->base == NULL || ty->kind != TY_ARRAY ||
        ty->base == NULL) {
        return 0;
    }
    str_elem_sz = str->ty->base->size;
    return ty->base->size == str_elem_sz;
}

/* ---- expression parsing ---- */

static struct node *parse_unary(void);

/* primary expression */
static struct node *parse_primary(void)
{
    struct tok *t;
    struct node *n;
    struct symbol *sym;
    long eval;
    struct type *cast_ty;
    struct node head;
    struct node *cur_arg;
    struct node *arg;

    t = peek();

    /* number */
    if (t->kind == TOK_NUM) {
        advance();
        n = new_num(t->val);
        if (t->suffix_unsigned && t->suffix_long) {
            n->ty = ty_ulong;
        } else if (t->suffix_unsigned) {
            n->ty = ty_uint;
        } else if (t->suffix_long) {
            n->ty = ty_long;
        } else if (t->is_hex_or_oct &&
                   t->val > 2147483647L &&
                   (unsigned long)t->val <= 4294967295UL) {
            /* hex/octal that doesn't fit in int but fits
             * in unsigned int: C99 6.4.4.1 */
            n->ty = ty_uint;
        } else if (t->val > 2147483647L || t->val < -2147483648L) {
            n->ty = ty_long;
        }
        return n;
    }

    /* floating-point number */
    if (t->kind == TOK_FNUM) {
        advance();
        if (t->suffix_imaginary) {
            /* GNU imaginary literal: e.g. 1.0i -> _Complex with real=0, imag=fval */
            struct node *re;
            struct node *im;
            struct type *fty;

            fty = ty_double;
            if (t->suffix_float) {
                fty = ty_float;
            } else if (t->suffix_long && ty_ldouble != NULL) {
                fty = ty_ldouble;
            }
            re = new_node(ND_FNUM);
            re->fval = 0.0;
            re->label_id = new_label();
            re->ty = fty;
            im = new_node(ND_FNUM);
            im->fval = t->suffix_float ? (float)t->fval : t->fval;
            im->label_id = new_label();
            im->ty = fty;
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena,
                "__builtin_complex", 17);
            n->args = re;
            re->next = im;
            im->next = NULL;
            /* _Complex long double not represented yet; map to _Complex double. */
            n->ty = t->suffix_float ? ty_cfloat : ty_cdouble;
            return n;
        }
        n = new_node(ND_FNUM);
        n->fval = t->fval;
        n->label_id = new_label();
        if (t->suffix_float) {
            n->fval = (float)t->fval;
            n->ty = ty_float;
        } else if (t->suffix_long && ty_ldouble != NULL) {
            n->ty = ty_ldouble;
        } else {
            n->ty = ty_double;
        }
        return n;
    }

    /* character literal */
    if (t->kind == TOK_CHAR_LIT) {
        advance();
        n = new_num(t->val);
        n->ty = ty_int;
        return n;
    }

    /* string literal (with concatenation of adjacent strings) */
    if (t->kind == TOK_STR) {
        char cat_buf[STRING_LITERAL_MAX_BYTES];
        struct tok *parts[64];
        int part_widths[64];
        int nparts;
        int out_width;
        int out_len;
        int out_units;
        int i;
        int j;
        int pos;
        unsigned char *p;
        unsigned long cp;
        int consumed;

        parts[0] = t;
        part_widths[0] = string_token_width(t);
        nparts = 1;
        out_width = part_widths[0];
        advance();
        while (peek()->kind == TOK_STR && nparts < 64) {
            parts[nparts] = advance();
            part_widths[nparts] = string_token_width(parts[nparts]);
            if (part_widths[nparts] > out_width) {
                out_width = part_widths[nparts];
            }
            nparts++;
        }
        out_len = 0;
        out_units = 0;
        if (out_width == 1) {
            for (i = 0; i < nparts; i++) {
                for (j = 0; j < parts[i]->len &&
                            out_len < (int)sizeof(cat_buf) - 1; j++) {
                    cat_buf[out_len++] = parts[i]->str[j];
                }
            }
        } else {
            for (i = 0; i < nparts; i++) {
                p = (unsigned char *)parts[i]->str;
                pos = 0;
                while (pos < parts[i]->len &&
                       out_len < (int)sizeof(cat_buf) - 4) {
                    cp = decode_utf8_codepoint(p + pos,
                                               parts[i]->len - pos,
                                               &consumed);
                    pos += consumed;
                    out_len = append_wide_codepoint(cat_buf, out_len,
                        (int)sizeof(cat_buf) - 4, cp, out_width,
                        &out_units);
                }
            }
        }
        cat_buf[out_len] = '\0';
        n = new_node(ND_STR);
        n->name = str_dup(parse_arena, cat_buf, out_len);
        n->val = out_len;
        n->label_id = new_label();
        if (out_width == 2) {
            n->ty = type_array(ty_short, out_units + 1);
        } else if (out_width == 4) {
            n->ty = type_array(ty_int, out_units + 1);
        } else {
            n->ty = type_array(ty_char, out_len + 1);
        }
        return n;
    }

    /* parenthesized expression, cast, or GNU statement expression */
    if (t->kind == TOK_LPAREN) {
        advance();

        /* GNU statement expression: ({ stmt; stmt; expr; }) */
        if (peek()->kind == TOK_LBRACE) {
            struct node *blk;
            struct node *last;

            blk = parse_compound_stmt();

            /* find the last statement in the block -- its value
             * is the result of the statement expression */
            last = NULL;
            if (blk->body != NULL) {
                for (last = blk->body; last->next != NULL;
                     last = last->next)
                    ;
            }

            n = new_node(ND_STMT_EXPR);
            n->body = blk->body;
            /* type is the type of the last expression */
            if (last != NULL) {
                add_type(last);
                n->ty = last->ty ? last->ty : ty_int;
            } else {
                n->ty = ty_void;
            }

            expect(TOK_RPAREN, "')'");
            return n;
        }

        if (is_type_token(peek())) {
            cast_ty = parse_type_name();
            expect(TOK_RPAREN, "')'");

            if (peek()->kind == TOK_LBRACE) {
                /* compound literal: (type){ init-list } */
                struct node *ilist;
                struct type *lit_ty;

                /* parse_type_name() can return shared typedef types.
                 * Copy before inferring array length so one compound
                 * literal does not mutate later uses of the same typedef. */
                lit_ty = NULL;
                if (cast_ty != NULL) {
                    lit_ty = copy_type(cast_ty);
                }
                cast_ty = lit_ty;
                ilist = parse_global_init(cast_ty);
                /* unsized array: infer length from init list */
                if (ilist != NULL && ilist->kind == ND_INIT_LIST &&
                    cast_ty->kind == TY_ARRAY && cast_ty->base &&
                    (cast_ty->array_len == 0 || cast_ty->size == 0)) {
                    int cnt;

                    cnt = infer_unsized_array_len(cast_ty, ilist);
                    set_inferred_array_len(cast_ty, cnt);
                }
                n = new_node(ND_COMPOUND_LIT);
                n->ty = cast_ty;
                n->body = ilist;
                return n;
            }

            n = new_unary(ND_CAST, parse_unary());
            n->ty = cast_ty;
            return n;
        }

        n = parse_expr();
        expect(TOK_RPAREN, "')'");
        return n;
    }

    /* sizeof */
    if (t->kind == TOK_SIZEOF) {
        advance();
        if (peek()->kind == TOK_LPAREN) {
            advance();
            if (is_type_token(peek())) {
                cast_ty = parse_type_name();
                expect(TOK_RPAREN, "')'");
                /* VLA array: sizeof must be computed at runtime */
                if (cast_ty && cast_ty->is_vla &&
                    cast_ty->vla_expr != NULL &&
                    cast_ty->kind == TY_ARRAY) {
                    int esz;
                    esz = cast_ty->base ? cast_ty->base->size : 1;
                    n = new_binary(ND_MUL, cast_ty->vla_expr,
                                   new_num(esz));
                    n->ty = ty_ulong;
                    return n;
                }
                /* VLA struct: struct containing VLA member.
                 * type_complete_struct stores the element size
                 * in the struct's size field. */
                if (cast_ty && cast_ty->kind == TY_STRUCT &&
                    cast_ty->is_vla && cast_ty->vla_expr != NULL) {
                    int vesz;
                    vesz = (cast_ty->size > 0) ? cast_ty->size : 1;
                    n = new_binary(ND_MUL, cast_ty->vla_expr,
                                   new_num(vesz));
                    n->ty = ty_ulong;
                    return n;
                }
                return new_ulong(type_size(cast_ty));
            }
            n = parse_expr();
            expect(TOK_RPAREN, "')'");
            add_type(n);
            /* VLA array: sizeof must be computed at runtime */
            if (n->ty && n->ty->is_vla &&
                n->ty->vla_expr != NULL &&
                n->ty->kind == TY_ARRAY) {
                int esz2;
                esz2 = n->ty->base ? n->ty->base->size : 1;
                n = new_binary(ND_MUL, n->ty->vla_expr,
                               new_num(esz2));
                n->ty = ty_ulong;
                return n;
            }
            return new_ulong(type_size(n->ty));
        }
        n = parse_unary();
        add_type(n);
        /* VLA array: sizeof must be computed at runtime */
        if (n->ty && n->ty->is_vla &&
            n->ty->vla_expr != NULL &&
            n->ty->kind == TY_ARRAY) {
            int esz3;
            esz3 = n->ty->base ? n->ty->base->size : 1;
            n = new_binary(ND_MUL, n->ty->vla_expr,
                           new_num(esz3));
            n->ty = ty_ulong;
            return n;
        }
        return new_ulong(type_size(n->ty));
    }

    /* C11: _Alignof / GNU __alignof__ / __alignof */
    if ((t->kind == TOK_ALIGNOF && cc_has_feat(FEAT_ALIGNOF)) ||
        (t->kind == TOK_IDENT && t->str &&
         (strcmp(t->str, "__alignof__") == 0 ||
          strcmp(t->str, "__alignof") == 0))) {
        advance();
        expect(TOK_LPAREN, "'('");
        if (is_type_token(peek())) {
            cast_ty = parse_type_name();
        } else {
            n = parse_assign();
            add_type(n);
            cast_ty = n->ty;
        }
        expect(TOK_RPAREN, "')'");
        return new_num(type_align(cast_ty));
    }

    /* C11: _Generic */
    if (t->kind == TOK_GENERIC && cc_has_feat(FEAT_GENERIC)) {
        struct node *ctrl_expr;
        struct type *ctrl_ty;
        struct type *assoc_ty;
        struct node *result;
        struct node *default_result;
        int found;

        advance();
        expect(TOK_LPAREN, "'('");
        ctrl_expr = parse_assign();
        add_type(ctrl_expr);
        ctrl_ty = ctrl_expr->ty;

        /* C11 6.5.1.1p2: lvalue conversion on controlling type:
         * array-to-pointer, function-to-pointer decay. */
        if (ctrl_ty && ctrl_ty->kind == TY_ARRAY) {
            ctrl_ty = type_ptr(ctrl_ty->base);
        }
        if (ctrl_ty && ctrl_ty->kind == TY_FUNC) {
            ctrl_ty = type_ptr(ctrl_ty);
        }

        expect(TOK_COMMA, "','");

        result = NULL;
        default_result = NULL;
        found = 0;
        while (peek()->kind != TOK_RPAREN && peek()->kind != TOK_EOF) {
            if (peek()->kind == TOK_DEFAULT) {
                advance();
                expect(TOK_COLON, "':'");
                n = parse_assign();
                if (!found) {
                    default_result = n;
                }
            } else {
                assoc_ty = parse_type_name();
                expect(TOK_COLON, "':'");
                n = parse_assign();
                if (!found && ctrl_ty && assoc_ty &&
                    c11_generic_match(ctrl_ty, assoc_ty)) {
                    result = n;
                    found = 1;
                }
            }
            if (peek()->kind != TOK_COMMA) {
                break;
            }
            advance();
        }
        expect(TOK_RPAREN, "')'");
        if (result) {
            add_type(result);
            return result;
        }
        if (default_result) {
            add_type(default_result);
            return default_result;
        }
        return new_num(0);
    }

    /* C11: _Static_assert */
    if ((t->kind == TOK_STATIC_ASSERT && cc_has_feat(FEAT_STATIC_ASSERT)) ||
        (t->kind == TOK_STATIC_ASSERT_KW &&
         cc_has_feat2(FEAT2_STATIC_ASSERT_NS))) {
        struct node *sa_expr;
        advance();
        expect(TOK_LPAREN, "'('");
        sa_expr = parse_assign();
        add_type(sa_expr);
        if (peek()->kind == TOK_COMMA) {
            advance();
            /* parse message expression (handles string concat) */
            parse_assign();
        }
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");
        if (sa_expr->kind == ND_NUM && sa_expr->val == 0) {
            diag_warn(t->file, t->line, t->col,
                      "static assertion failed (warning)");
        }
        return new_node(ND_BLOCK); /* no-op */
    }

    /* C23: true/false constants */
    if (t->kind == TOK_TRUE && cc_has_feat(FEAT_BOOL_KW)) {
        advance();
        n = new_num(1);
        n->ty = ty_bool;
        return n;
    }
    if (t->kind == TOK_FALSE && cc_has_feat(FEAT_BOOL_KW)) {
        advance();
        n = new_num(0);
        n->ty = ty_bool;
        return n;
    }

    /* C23: nullptr */
    if (t->kind == TOK_NULLPTR && cc_has_feat(FEAT_NULLPTR)) {
        advance();
        n = new_num(0);
        n->ty = type_ptr(ty_void);
        return n;
    }

    /* identifier */
    if (t->kind == TOK_IDENT) {
        advance();

        /* GNU __real__ / __imag__ unary operators */
        if (t->str &&
            (strcmp(t->str, "__real__") == 0 ||
             strcmp(t->str, "__real") == 0)) {
            struct node *mn;
            n = parse_unary();
            add_type(n);
            /* __real__ on complex: extract real part (offset 0) */
            if (n->ty && n->ty->kind == TY_COMPLEX_DOUBLE) {
                mn = new_unary(ND_MEMBER, n);
                mn->offset = 0;
                mn->ty = ty_double;
                return mn;
            }
            if (n->ty && n->ty->kind == TY_COMPLEX_FLOAT) {
                mn = new_unary(ND_MEMBER, n);
                mn->offset = 0;
                mn->ty = ty_float;
                return mn;
            }
            /* __real__ on non-complex type is identity */
            return n;
        }
        if (t->str &&
            (strcmp(t->str, "__imag__") == 0 ||
             strcmp(t->str, "__imag") == 0)) {
            struct node *mn;
            n = parse_unary();
            add_type(n);
            /* __imag__ on complex: extract imag part */
            if (n->ty && n->ty->kind == TY_COMPLEX_DOUBLE) {
                mn = new_unary(ND_MEMBER, n);
                mn->offset = 8;
                mn->ty = ty_double;
                return mn;
            }
            if (n->ty && n->ty->kind == TY_COMPLEX_FLOAT) {
                mn = new_unary(ND_MEMBER, n);
                mn->offset = 4;
                mn->ty = ty_float;
                return mn;
            }
            /* __imag__ on non-complex returns 0 */
            return new_num(0);
        }

        if (find_enum_val(t->str, &eval)) {
            return new_num(eval);
        }

        /* __func__, __FUNCTION__, __PRETTY_FUNCTION__ */
        if (t->str &&
            (strcmp(t->str, "__func__") == 0 ||
             strcmp(t->str, "__FUNCTION__") == 0 ||
             strcmp(t->str, "__PRETTY_FUNCTION__") == 0)) {
            const char *fname;
            int flen;
            fname = current_func_name ? current_func_name : "";
            flen = (int)strlen(fname);
            n = new_node(ND_STR);
            n->name = str_dup(parse_arena, fname, flen);
            n->val = flen;
            n->label_id = new_label();
            n->ty = type_array(ty_char, flen + 1);
            return n;
        }

        /* __builtin_va_start(ap, last_named) */
        if (t->str && strcmp(t->str, "__builtin_va_start") == 0) {
            struct node *ap_expr;

            expect(TOK_LPAREN, "'('");
            ap_expr = parse_assign();
            add_type(ap_expr);
            expect(TOK_COMMA, "','");
            /* skip last named arg -- not needed at runtime */
            parse_assign();
            expect(TOK_RPAREN, "')'");

            n = new_node(ND_VA_START);
            n->lhs = ap_expr;
            n->ty = ty_void;
            return n;
        }

        /* __builtin_va_arg(ap, type) */
        if (t->str && strcmp(t->str, "__builtin_va_arg") == 0) {
            struct type *arg_ty;
            struct node *ap_expr;

            expect(TOK_LPAREN, "'('");
            ap_expr = parse_assign();
            add_type(ap_expr);
            expect(TOK_COMMA, "','");
            arg_ty = parse_type_name();
            expect(TOK_RPAREN, "')'");

            n = new_node(ND_VA_ARG);
            n->lhs = ap_expr;
            n->ty = arg_ty;
            return n;
        }

        /* __builtin_va_end(ap) -- no-op */
        if (t->str && strcmp(t->str, "__builtin_va_end") == 0) {
            expect(TOK_LPAREN, "'('");
            parse_assign();
            expect(TOK_RPAREN, "')'");
            return new_num(0);
        }

        /* __builtin_{add,sub,mul}_overflow(a, b, &result) */
        if (t->str &&
            (strcmp(t->str, "__builtin_add_overflow") == 0 ||
             strcmp(t->str, "__builtin_sub_overflow") == 0 ||
             strcmp(t->str, "__builtin_mul_overflow") == 0)) {
            struct node *a_expr;
            struct node *b_expr;
            struct node *res_expr;
            int op;

            if (t->str[10] == 'a') op = 0;      /* add */
            else if (t->str[10] == 's') op = 1;  /* sub */
            else op = 2;                          /* mul */

            expect(TOK_LPAREN, "'('");
            a_expr = parse_assign();
            add_type(a_expr);
            expect(TOK_COMMA, "','");
            b_expr = parse_assign();
            add_type(b_expr);
            expect(TOK_COMMA, "','");
            res_expr = parse_assign();
            add_type(res_expr);
            expect(TOK_RPAREN, "')'");

            n = new_node(ND_BUILTIN_OVERFLOW);
            n->lhs = a_expr;
            n->rhs = b_expr;
            n->body = res_expr;
            n->val = op;
            n->ty = ty_int;
            return n;
        }

        /* __builtin_frame_address(0) -- return x29 */
        if (t->str &&
            strcmp(t->str, "__builtin_frame_address") == 0) {
            expect(TOK_LPAREN, "'('");
            parse_assign(); /* skip the argument (must be 0) */
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena,
                "__builtin_frame_address", 23);
            n->ty = type_ptr(ty_void);
            n->args = NULL;
            return n;
        }

        /* __builtin_return_address(N) -- return saved LR from Nth frame */
        if (t->str &&
            strcmp(t->str, "__builtin_return_address") == 0) {
            struct node *depth_arg;
            expect(TOK_LPAREN, "'('");
            depth_arg = parse_assign();
            add_type(depth_arg);
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena,
                "__builtin_return_address", 24);
            n->ty = type_ptr(ty_void);
            n->args = depth_arg;
            /* store depth in val for codegen */
            if (depth_arg && depth_arg->kind == ND_NUM) {
                n->val = depth_arg->val;
            }
            return n;
        }

        /* __builtin_types_compatible_p(type1, type2) */
        if (t->str &&
            strcmp(t->str, "__builtin_types_compatible_p") == 0) {
            struct type *ty1;
            struct type *ty2;
            int compat;

            expect(TOK_LPAREN, "'('");
            ty1 = parse_type_name();
            expect(TOK_COMMA, "','");
            ty2 = parse_type_name();
            expect(TOK_RPAREN, "')'");

            compat = 0;
            if (ty1 && ty2) {
                compat = type_is_compatible(ty1, ty2) ? 1 : 0;
            }
            return new_num((long)compat);
        }

        /* __builtin_choose_expr(const_expr, expr1, expr2) */
        if (t->str &&
            strcmp(t->str, "__builtin_choose_expr") == 0) {
            struct node *cond_expr;
            struct node *e1;
            struct node *e2;

            expect(TOK_LPAREN, "'('");
            cond_expr = parse_assign();
            add_type(cond_expr);
            expect(TOK_COMMA, "','");
            e1 = parse_assign();
            add_type(e1);
            expect(TOK_COMMA, "','");
            e2 = parse_assign();
            add_type(e2);
            expect(TOK_RPAREN, "')'");

            /* compile-time selection: pick e1 if const nonzero */
            if (cond_expr->kind == ND_NUM && cond_expr->val != 0) {
                return e1;
            }
            return e2;
        }

        /* __builtin_constant_p(expr) */
        if (t->str &&
            strcmp(t->str, "__builtin_constant_p") == 0) {
            struct node *bcp_expr;
            expect(TOK_LPAREN, "'('");
            bcp_expr = parse_assign();
            bcp_expr = constant_fold(bcp_expr);
            expect(TOK_RPAREN, "')'");
            return new_num(bcp_expr->kind == ND_NUM ? 1 : 0);
        }

        /* __builtin_expect(expr, expected) */
        if (t->str &&
            strcmp(t->str, "__builtin_expect") == 0) {
            struct node *val_expr;
            expect(TOK_LPAREN, "'('");
            val_expr = parse_assign();
            add_type(val_expr);
            expect(TOK_COMMA, "','");
            parse_assign(); /* discard hint */
            expect(TOK_RPAREN, "')'");
            return val_expr;
        }

        /* __builtin_offsetof(type, member.chain[idx]...) */
        if (t->str &&
            strcmp(t->str, "__builtin_offsetof") == 0) {
            struct type *oty;
            struct member *om;
            struct node *result;
            long base_off;

            expect(TOK_LPAREN, "'('");
            oty = parse_type_name();
            expect(TOK_COMMA, "','");

            /* parse first member name */
            t = expect(TOK_IDENT, "member name");
            base_off = 0;
            result = NULL;
            if (oty && (oty->kind == TY_STRUCT ||
                        oty->kind == TY_UNION)) {
                om = type_find_member(oty, t->str);
                if (om) {
                    base_off = (long)om->offset;
                    oty = om->ty;
                }
            }
            result = new_num(base_off);

            /* parse chained .member and [index] */
            while (peek()->kind == TOK_DOT ||
                   peek()->kind == TOK_LBRACKET) {
                if (peek()->kind == TOK_DOT) {
                    advance();
                    t = expect(TOK_IDENT, "member name");
                    if (oty && (oty->kind == TY_STRUCT ||
                                oty->kind == TY_UNION)) {
                        om = type_find_member(oty, t->str);
                        if (om) {
                            result = new_binary(ND_ADD, result,
                                new_num((long)om->offset));
                            add_type(result);
                            oty = om->ty;
                        }
                    }
                } else {
                    /* [index] */
                    struct node *idx;
                    int elem_size;

                    advance(); /* consume '[' */
                    idx = parse_expr();
                    expect(TOK_RBRACKET, "']'");
                    elem_size = 1;
                    if (oty && oty->kind == TY_ARRAY) {
                        elem_size = oty->base ? oty->base->size : 1;
                        oty = oty->base;
                    } else if (oty) {
                        elem_size = oty->size;
                    }
                    add_type(idx);
                    result = new_binary(ND_ADD, result,
                        new_binary(ND_MUL, idx,
                            new_num((long)elem_size)));
                    add_type(result);
                }
            }

            expect(TOK_RPAREN, "')'");
            result = constant_fold(result);
            return result;
        }

        /* __builtin_prefetch(addr, ...) -- no-op, discard args */
        if (t->str &&
            strcmp(t->str, "__builtin_prefetch") == 0) {
            expect(TOK_LPAREN, "'('");
            parse_assign(); /* discard addr */
            while (peek()->kind == TOK_COMMA) {
                advance();
                parse_assign(); /* discard rw/locality hints */
            }
            expect(TOK_RPAREN, "')'");
            return new_num(0);
        }

        /* __builtin_classify_type(expr) -- return 0 (integer) */
        if (t->str &&
            strcmp(t->str, "__builtin_classify_type") == 0) {
            struct node *cte;
            expect(TOK_LPAREN, "'('");
            cte = parse_assign();
            add_type(cte);
            expect(TOK_RPAREN, "')'");
            if (cte->ty) {
                switch (cte->ty->kind) {
                case TY_VOID:   return new_num(0);
                case TY_INT:
                case TY_LONG:
                case TY_SHORT:
                case TY_CHAR:
                case TY_BOOL:   return new_num(1);
                case TY_FLOAT:
                case TY_DOUBLE: return new_num(8);
                case TY_PTR:    return new_num(5);
                default:        break;
                }
            }
            return new_num(1);
        }

        /* __builtin_signbit(x) -- check sign bit of float/double */
        if (t->str &&
            (strcmp(t->str, "__builtin_signbit") == 0 ||
             strcmp(t->str, "__builtin_signbitf") == 0 ||
             strcmp(t->str, "__builtin_signbitl") == 0)) {
            struct node *val_expr;
            expect(TOK_LPAREN, "'('");
            val_expr = parse_assign();
            add_type(val_expr);
            expect(TOK_RPAREN, "')'");
            /* pass through as function call for gen.c to handle */
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena, t->str, t->len);
            n->args = val_expr;
            val_expr->next = NULL;
            n->ty = ty_int;
            return n;
        }

        /* __builtin_copysign(x, y) -- copy sign from y to x */
        if (t->str &&
            (strcmp(t->str, "__builtin_copysign") == 0 ||
             strcmp(t->str, "__builtin_copysignf") == 0 ||
             strcmp(t->str, "__builtin_copysignl") == 0)) {
            struct node *x_expr;
            struct node *y_expr;
            expect(TOK_LPAREN, "'('");
            x_expr = parse_assign();
            add_type(x_expr);
            expect(TOK_COMMA, "','");
            y_expr = parse_assign();
            add_type(y_expr);
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena, t->str, t->len);
            n->args = x_expr;
            x_expr->next = y_expr;
            y_expr->next = NULL;
            n->ty = ty_double;
            return n;
        }

        /* __builtin_longjmp(buf, val) -- non-local jump */
        if (t->str &&
            strcmp(t->str, "__builtin_longjmp") == 0) {
            struct node *buf_expr;
            struct node *val_expr;
            expect(TOK_LPAREN, "'('");
            buf_expr = parse_assign();
            add_type(buf_expr);
            expect(TOK_COMMA, "','");
            val_expr = parse_assign();
            add_type(val_expr);
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena,
                "__builtin_longjmp", 18);
            n->args = buf_expr;
            buf_expr->next = val_expr;
            val_expr->next = NULL;
            n->ty = ty_void;
            return n;
        }

        /* __builtin_setjmp(buf) -- set jump target */
        if (t->str &&
            strcmp(t->str, "__builtin_setjmp") == 0) {
            struct node *buf_expr;
            expect(TOK_LPAREN, "'('");
            buf_expr = parse_assign();
            add_type(buf_expr);
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena,
                "__builtin_setjmp", 16);
            n->args = buf_expr;
            buf_expr->next = NULL;
            n->ty = ty_int;
            return n;
        }

        /* __builtin_complex(real, imag) -- create complex value */
        if (t->str &&
            strcmp(t->str, "__builtin_complex") == 0) {
            struct node *re_expr;
            struct node *im_expr;
            expect(TOK_LPAREN, "'('");
            re_expr = parse_assign();
            add_type(re_expr);
            expect(TOK_COMMA, "','");
            im_expr = parse_assign();
            add_type(im_expr);
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena,
                "__builtin_complex", 17);
            n->args = re_expr;
            re_expr->next = im_expr;
            im_expr->next = NULL;
            n->ty = ty_cdouble;
            return n;
        }

        /* __builtin_conj(x), __builtin_conjf(x), __builtin_conjl(x) */
        if (t->str &&
            (strcmp(t->str, "__builtin_conj") == 0 ||
             strcmp(t->str, "__builtin_conjf") == 0 ||
             strcmp(t->str, "__builtin_conjl") == 0)) {
            struct node *val_expr;

            expect(TOK_LPAREN, "'('");
            val_expr = parse_assign();
            add_type(val_expr);
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena, t->str, t->len);
            n->args = val_expr;
            val_expr->next = NULL;
            if (strcmp(t->str, "__builtin_conjf") == 0 ||
                (val_expr->ty != NULL &&
                 val_expr->ty->kind == TY_COMPLEX_FLOAT)) {
                n->ty = ty_cfloat;
            } else {
                n->ty = ty_cdouble;
            }
            return n;
        }

        /* __builtin_expect_with_probability(x, v, prob) */
        if (t->str &&
            strcmp(t->str,
                "__builtin_expect_with_probability") == 0) {
            struct node *val_expr;
            expect(TOK_LPAREN, "'('");
            val_expr = parse_assign();
            add_type(val_expr);
            expect(TOK_COMMA, "','");
            parse_assign(); /* discard expected value */
            expect(TOK_COMMA, "','");
            parse_assign(); /* discard probability */
            expect(TOK_RPAREN, "')'");
            return val_expr;
        }

        /* __builtin_isnan(x), __builtin_isinf(x),
         * __builtin_isfinite(x) */
        if (t->str &&
            (strcmp(t->str, "__builtin_isnan") == 0 ||
             strcmp(t->str, "__builtin_isinf") == 0 ||
             strcmp(t->str, "__builtin_isfinite") == 0 ||
             strcmp(t->str, "__builtin_isinf_sign") == 0)) {
            struct node *val_expr;
            expect(TOK_LPAREN, "'('");
            val_expr = parse_assign();
            add_type(val_expr);
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena, t->str, t->len);
            n->args = val_expr;
            val_expr->next = NULL;
            n->ty = ty_int;
            return n;
        }

        /* __builtin_fabs(x), __builtin_fabsf(x) */
        if (t->str &&
            (strcmp(t->str, "__builtin_fabs") == 0 ||
             strcmp(t->str, "__builtin_fabsf") == 0 ||
             strcmp(t->str, "__builtin_fabsl") == 0)) {
            struct node *val_expr;
            expect(TOK_LPAREN, "'('");
            val_expr = parse_assign();
            add_type(val_expr);
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena, t->str, t->len);
            n->args = val_expr;
            val_expr->next = NULL;
            n->ty = ty_double;
            return n;
        }

        /* __builtin_abs(x), __builtin_labs(x) */
        if (t->str &&
            (strcmp(t->str, "__builtin_abs") == 0 ||
             strcmp(t->str, "__builtin_labs") == 0)) {
            struct node *val_expr;
            expect(TOK_LPAREN, "'('");
            val_expr = parse_assign();
            add_type(val_expr);
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena, t->str, t->len);
            n->args = val_expr;
            val_expr->next = NULL;
            n->ty = ty_long;
            return n;
        }

        /* __builtin_sqrt(x), __builtin_sqrtf(x) */
        if (t->str &&
            (strcmp(t->str, "__builtin_sqrt") == 0 ||
             strcmp(t->str, "__builtin_sqrtf") == 0)) {
            struct node *val_expr;
            expect(TOK_LPAREN, "'('");
            val_expr = parse_assign();
            add_type(val_expr);
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena, t->str, t->len);
            n->args = val_expr;
            val_expr->next = NULL;
            n->ty = ty_double;
            return n;
        }

        /* __builtin_inf() / __builtin_huge_val() -> +Inf double */
        if (t->str &&
            (strcmp(t->str, "__builtin_inf") == 0 ||
             strcmp(t->str, "__builtin_huge_val") == 0)) {
            expect(TOK_LPAREN, "'('");
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena, t->str, t->len);
            n->args = NULL;
            n->ty = ty_double;
            return n;
        }
        /* __builtin_inff() / __builtin_huge_valf() -> +Inf float */
        if (t->str &&
            (strcmp(t->str, "__builtin_inff") == 0 ||
             strcmp(t->str, "__builtin_huge_valf") == 0)) {
            expect(TOK_LPAREN, "'('");
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena, t->str, t->len);
            n->args = NULL;
            n->ty = ty_float;
            return n;
        }
        /* __builtin_nan(str) -> quiet NaN double */
        if (t->str &&
            strcmp(t->str, "__builtin_nan") == 0) {
            expect(TOK_LPAREN, "'('");
            parse_assign(); /* discard string arg */
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena,
                "__builtin_nan", 13);
            n->args = NULL;
            n->ty = ty_double;
            return n;
        }
        /* __builtin_nanf(str) -> quiet NaN float */
        if (t->str &&
            strcmp(t->str, "__builtin_nanf") == 0) {
            expect(TOK_LPAREN, "'('");
            parse_assign(); /* discard string arg */
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena,
                "__builtin_nanf", 14);
            n->args = NULL;
            n->ty = ty_float;
            return n;
        }

        /* __builtin_trap() -> emit trap instruction */
        if (t->str &&
            strcmp(t->str, "__builtin_trap") == 0 &&
            peek()->kind == TOK_LPAREN) {
            expect(TOK_LPAREN, "'('");
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena,
                "__builtin_trap", 14);
            n->args = NULL;
            n->ty = ty_void;
            return n;
        }

        /* __builtin_unreachable() -> emit trap/unreachable */
        if (t->str &&
            strcmp(t->str, "__builtin_unreachable") == 0 &&
            peek()->kind == TOK_LPAREN) {
            expect(TOK_LPAREN, "'('");
            expect(TOK_RPAREN, "')'");
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena,
                "__builtin_unreachable", 21);
            n->args = NULL;
            n->ty = ty_void;
            return n;
        }

        /* __builtin_puts -> puts, __builtin_snprintf -> snprintf */
        if (t->str &&
            strncmp(t->str, "__builtin_", 10) == 0 &&
            peek()->kind == TOK_LPAREN) {
            const char *real_name;
            real_name = NULL;
            if (strcmp(t->str, "__builtin_puts") == 0)
                real_name = "puts";
            else if (strcmp(t->str, "__builtin_snprintf") == 0)
                real_name = "snprintf";
            else if (strcmp(t->str, "__builtin_printf") == 0)
                real_name = "printf";
            else if (strcmp(t->str, "__builtin_sprintf") == 0)
                real_name = "sprintf";
            else if (strcmp(t->str, "__builtin_memcpy") == 0)
                real_name = "memcpy";
            else if (strcmp(t->str, "__builtin_memset") == 0)
                real_name = "memset";
            else if (strcmp(t->str, "__builtin_strcmp") == 0)
                real_name = "strcmp";
            else if (strcmp(t->str, "__builtin_strlen") == 0)
                real_name = "strlen";
            else if (strcmp(t->str, "__builtin_strcpy") == 0)
                real_name = "strcpy";
            else if (strcmp(t->str, "__builtin_abort") == 0)
                real_name = "abort";
            else if (strcmp(t->str, "__builtin_exit") == 0)
                real_name = "exit";
            if (real_name) {
                t->str = str_dup(parse_arena, real_name,
                                 (int)strlen(real_name));
                t->len = (int)strlen(real_name);
                /* fall through to function call */
            }
        }

        /* function call */
        if (peek()->kind == TOK_LPAREN) {
            advance();
            n = new_node(ND_CALL);
            n->name = str_dup(parse_arena, t->str, t->len);

            sym = find_symbol(t->str);
            /* use asm_label for mangled nested functions */
            if (sym && sym->asm_label) {
                n->name = sym->asm_label;
            }
            if (sym && sym->ty && sym->ty->kind == TY_PTR &&
                sym->ty->base && sym->ty->base->kind == TY_FUNC) {
                /* function pointer call: set lhs to variable ref
                 * so codegen emits blr (indirect call) */
                struct node *var_ref;
                n->ty = sym->ty->base->ret;
                var_ref = new_node(ND_VAR);
                var_ref->name = n->name;
                var_ref->ty = sym->ty;
                var_ref->offset = sym->offset;
                n->lhs = var_ref;
            } else if (sym && sym->ty && sym->ty->kind == TY_FUNC) {
                struct node *var_ref;
                n->ty = sym->ty->ret;
                var_ref = new_node(ND_VAR);
                var_ref->name = n->name;
                var_ref->ty = sym->ty;
                var_ref->offset = sym->offset;
                n->lhs = var_ref;


            } else if (sym == NULL) {
                /* implicit function declaration: int func() (C89) */
                struct type *impl_fty;
                impl_fty = type_func(ty_int, NULL);
                sym = add_global(n->name, impl_fty);
                n->ty = ty_int;
            } else {
                n->ty = ty_int;
            }

            head.next = NULL;
            cur_arg = &head;

            {
                struct type *par_ty;
                par_ty = (sym && sym->ty &&
                          sym->ty->kind == TY_FUNC)
                         ? sym->ty->params : NULL;

                while (peek()->kind != TOK_RPAREN &&
                       peek()->kind != TOK_EOF) {
                    arg = parse_assign();
                    add_type(arg);
                    /* insert implicit cast for type mismatch */
                    if (par_ty != NULL && arg->ty != NULL &&
                        arg->ty->kind != par_ty->kind) {
                        int afp, pfp;
                        afp = type_is_flonum(arg->ty);
                        pfp = type_is_flonum(par_ty);
                        if (afp != pfp ||
                            arg->ty->size != par_ty->size) {
                            struct node *cast;
                            cast = new_unary(ND_CAST, arg);
                            cast->ty = par_ty;
                            arg = cast;
                        }
                    }
                    cur_arg->next = arg;
                    cur_arg = arg;
                    cur_arg->next = NULL;
                    if (par_ty != NULL)
                        par_ty = par_ty->next;
                    if (peek()->kind != TOK_COMMA) {
                        break;
                    }
                    advance();
                }
            }
            n->args = head.next;

            expect(TOK_RPAREN, "')'");
            return n;
        }

        /* variable reference */
        {
            int is_upvar;
            is_upvar = 0;
            sym = find_local_ex(t->str, &is_upvar);
            if (sym == NULL) {
                sym = find_global(t->str);
            }
            if (sym == NULL) {
                sym = add_global(t->str, ty_int);
            }

            n = new_node(ND_VAR);
            /* use asm_label for static locals (mangled name) */
            if (sym->asm_label) {
                n->name = sym->asm_label;
            } else {
                n->name = str_dup(parse_arena, t->str, t->len);
            }
            n->ty = sym->ty;
            n->offset = sym->offset;
            n->attr_flags = sym->attr_flags;
            n->is_upvar = is_upvar;
            return n;
        }
    }

    diag_error(t->file, t->line, t->col,
               "expected expression, got token kind %d", (int)t->kind);
    advance();
    return new_num(0);
}

/* postfix expression */
static struct node *parse_postfix(void)
{
    struct node *n;
    struct tok *t;
    struct member *m;
    struct node *idx;
    struct node *mn;

    n = parse_primary();

    for (;;) {
        /* array subscript */
        if (peek()->kind == TOK_LBRACKET) {
            advance();
            idx = parse_expr();
            add_type(n);
            add_type(idx);
            n = new_unary(ND_DEREF, new_binary(ND_ADD, n, idx));
            add_type(n);
            expect(TOK_RBRACKET, "']'");
            continue;
        }

        /* member access . */
        if (peek()->kind == TOK_DOT) {
            advance();
            add_type(n);
            t = expect(TOK_IDENT, "member name");
            if (n->ty && (n->ty->kind == TY_STRUCT || n->ty->kind == TY_UNION)) {
                m = type_find_member(n->ty, t->str);
                if (m) {
                    mn = new_node(ND_MEMBER);
                    mn->lhs = n;
                    mn->name = str_dup(parse_arena, t->str, t->len);
                    mn->ty = m->ty;
                    mn->offset = m->offset;
                    mn->bit_width = m->bit_width;
                    mn->bit_offset = m->bit_offset;
                    n = mn;
                } else {
                    diag_error(t->file, t->line, t->col,
                               "no member '%s' in struct/union",
                               t->str);
                    n->ty = ty_int;
                }
            } else {
                diag_error(t->file, t->line, t->col,
                           "member access on non-struct type");
            }
            continue;
        }

        /* member access -> */
        if (peek()->kind == TOK_ARROW) {
            advance();
            add_type(n);
            t = expect(TOK_IDENT, "member name");
            n = new_unary(ND_DEREF, n);
            add_type(n);
            if (n->ty && (n->ty->kind == TY_STRUCT || n->ty->kind == TY_UNION)) {
                m = type_find_member(n->ty, t->str);
                if (m) {
                    mn = new_node(ND_MEMBER);
                    mn->lhs = n;
                    mn->name = str_dup(parse_arena, t->str, t->len);
                    mn->ty = m->ty;
                    mn->offset = m->offset;
                    mn->bit_width = m->bit_width;
                    mn->bit_offset = m->bit_offset;
                    n = mn;
                } else {
                    diag_error(t->file, t->line, t->col,
                               "no member '%s' in struct/union",
                               t->str);
                }
            }
            continue;
        }

        /* post-increment */
        if (peek()->kind == TOK_INC) {
            advance();
            n = new_unary(ND_POST_INC, n);
            add_type(n);
            continue;
        }

        /* post-decrement */
        if (peek()->kind == TOK_DEC) {
            advance();
            n = new_unary(ND_POST_DEC, n);
            add_type(n);
            continue;
        }

        /* function call through expression (function pointer, member) */
        if (peek()->kind == TOK_LPAREN) {
            struct node call_head;
            struct node *call_cur;
            struct node *call_arg;
            struct node *cn;
            struct type *fty;
            struct type *param_ty;

            advance();
            add_type(n);
            cn = new_node(ND_CALL);
            cn->lhs = n;

            /* resolve function type for param matching */
            fty = NULL;
            if (n->ty && n->ty->kind == TY_PTR &&
                n->ty->base && n->ty->base->kind == TY_FUNC) {
                cn->ty = n->ty->base->ret;
                fty = n->ty->base;
            } else if (n->ty && n->ty->kind == TY_FUNC) {
                cn->ty = n->ty->ret;
                fty = n->ty;
            } else {
                cn->ty = ty_int;
            }
            if (n->name) {
                cn->name = n->name;
            }
            call_head.next = NULL;
            call_cur = &call_head;
            param_ty = (fty != NULL) ? fty->params : NULL;
            while (peek()->kind != TOK_RPAREN &&
                   peek()->kind != TOK_EOF) {
                call_arg = parse_assign();
                add_type(call_arg);
                /* insert implicit cast for type mismatch */
                if (param_ty != NULL && call_arg->ty != NULL &&
                    call_arg->ty->kind != param_ty->kind) {
                    int from_fp, to_fp;
                    from_fp = type_is_flonum(call_arg->ty);
                    to_fp = type_is_flonum(param_ty);
                    if (from_fp != to_fp ||
                        call_arg->ty->size != param_ty->size) {
                        struct node *cast;
                        cast = new_unary(ND_CAST, call_arg);
                        cast->ty = param_ty;
                        call_arg = cast;
                    }
                }
                call_cur->next = call_arg;
                call_cur = call_arg;
                call_cur->next = NULL;
                if (param_ty != NULL)
                    param_ty = param_ty->next;
                if (peek()->kind != TOK_COMMA) break;
                advance();
            }
            cn->args = call_head.next;
            expect(TOK_RPAREN, "')'");
            n = cn;
            continue;
        }

        break;
    }

    return n;
}

/* unary expression */
static struct node *parse_unary(void)
{
    struct tok *t;
    struct node *n;

    t = peek();

    /* prefix ++ */
    if (t->kind == TOK_INC) {
        advance();
        n = new_unary(ND_PRE_INC, parse_unary());
        add_type(n);
        return n;
    }

    /* prefix -- */
    if (t->kind == TOK_DEC) {
        advance();
        n = new_unary(ND_PRE_DEC, parse_unary());
        add_type(n);
        return n;
    }

    /* computed goto: &&label (label address) */
    if (t->kind == TOK_AND) {
        struct tok *next_t;

        /* TOK_AND is the && token; peek ahead to see if
         * an identifier follows (making this &&label) */
        advance();
        next_t = peek();
        if (next_t->kind == TOK_IDENT) {
            advance();
            n = new_node(ND_LABEL_ADDR);
            n->name = str_dup(parse_arena,
                next_t->str, next_t->len);
            n->ty = type_ptr(ty_void);
            /* Label addresses need a section name when they appear
             * in static initializers, so prefer the current function
             * name and override it only for enclosing-function labels. */
            if (current_func_name != NULL) {
                n->section_name = str_dup(parse_arena,
                    current_func_name,
                    (int)strlen(current_func_name));
            }
            if (is_enclosing_label(next_t->str)) {
                n->section_name = str_dup(parse_arena,
                    enclosing_func_name,
                    (int)strlen(enclosing_func_name));
            }
            return n;
        }
        /* not &&label -- fall through would be parse error;
         * TOK_AND is a binary op, not expected here */
        diag_error(t->file, t->line, t->col,
                   "unexpected '&&' in expression");
    }

    /* address-of */
    if (t->kind == TOK_AMP) {
        advance();
        n = new_unary(ND_ADDR, parse_unary());
        add_type(n);
        return n;
    }

    /* dereference */
    if (t->kind == TOK_STAR) {
        advance();
        n = new_unary(ND_DEREF, parse_unary());
        add_type(n);
        return n;
    }

    /* unary plus */
    if (t->kind == TOK_PLUS) {
        advance();
        return parse_unary();
    }

    /* unary minus */
    if (t->kind == TOK_MINUS) {
        advance();
        n = new_binary(ND_SUB, new_num(0), parse_unary());
        add_type(n);
        n = constant_fold(n);
        return n;
    }

    /* logical not */
    if (t->kind == TOK_NOT) {
        advance();
        n = new_unary(ND_LOGNOT, parse_unary());
        add_type(n);
        n = constant_fold(n);
        return n;
    }

    /* bitwise not */
    if (t->kind == TOK_TILDE) {
        advance();
        n = new_unary(ND_BITNOT, parse_unary());
        add_type(n);
        n = constant_fold(n);
        return n;
    }

    /* GNU __extension__ as unary prefix in expressions (no-op) */
    if (t->kind == TOK_IDENT && t->str &&
        attr_is_extension_keyword(t)) {
        advance();
        return parse_unary();
    }

    return parse_postfix();
}

/* multiplicative */
static struct node *parse_mul(void)
{
    struct node *n;

    n = parse_unary();

    for (;;) {
        if (peek()->kind == TOK_STAR) {
            advance();
            n = new_binary(ND_MUL, n, parse_unary());
            add_type(n);
            n = constant_fold(n);
        } else if (peek()->kind == TOK_SLASH) {
            advance();
            n = new_binary(ND_DIV, n, parse_unary());
            add_type(n);
            n = constant_fold(n);
        } else if (peek()->kind == TOK_PERCENT) {
            advance();
            n = new_binary(ND_MOD, n, parse_unary());
            add_type(n);
            n = constant_fold(n);
        } else {
            break;
        }
    }
    return n;
}

/* additive */
static struct node *parse_add(void)
{
    struct node *n;

    n = parse_mul();

    for (;;) {
        if (peek()->kind == TOK_PLUS) {
            advance();
            n = new_binary(ND_ADD, n, parse_mul());
            add_type(n);
            n = constant_fold(n);
        } else if (peek()->kind == TOK_MINUS) {
            advance();
            n = new_binary(ND_SUB, n, parse_mul());
            add_type(n);
            n = constant_fold(n);
        } else {
            break;
        }
    }
    return n;
}

/* shift */
static struct node *parse_shift(void)
{
    struct node *n;

    n = parse_add();

    for (;;) {
        if (peek()->kind == TOK_LSHIFT) {
            advance();
            n = new_binary(ND_SHL, n, parse_add());
            add_type(n);
            n = constant_fold(n);
        } else if (peek()->kind == TOK_RSHIFT) {
            advance();
            n = new_binary(ND_SHR, n, parse_add());
            add_type(n);
            n = constant_fold(n);
        } else {
            break;
        }
    }
    return n;
}

/* relational */
static struct node *parse_relational(void)
{
    struct node *n;

    n = parse_shift();

    for (;;) {
        if (peek()->kind == TOK_LT) {
            advance();
            n = new_binary(ND_LT, n, parse_shift());
            add_type(n);
            n = constant_fold(n);
        } else if (peek()->kind == TOK_GT) {
            advance();
            /* a > b  ==  b < a */
            n = new_binary(ND_LT, parse_shift(), n);
            add_type(n);
            n = constant_fold(n);
        } else if (peek()->kind == TOK_LE) {
            advance();
            n = new_binary(ND_LE, n, parse_shift());
            add_type(n);
            n = constant_fold(n);
        } else if (peek()->kind == TOK_GE) {
            advance();
            /* a >= b  ==  b <= a */
            n = new_binary(ND_LE, parse_shift(), n);
            add_type(n);
            n = constant_fold(n);
        } else {
            break;
        }
    }
    return n;
}

/* equality */
static struct node *parse_equality(void)
{
    struct node *n;

    n = parse_relational();

    for (;;) {
        if (peek()->kind == TOK_EQ) {
            advance();
            n = new_binary(ND_EQ, n, parse_relational());
            add_type(n);
            n = constant_fold(n);
        } else if (peek()->kind == TOK_NE) {
            advance();
            n = new_binary(ND_NE, n, parse_relational());
            add_type(n);
            n = constant_fold(n);
        } else {
            break;
        }
    }
    return n;
}

/* bitwise and */
static struct node *parse_bitand(void)
{
    struct node *n;

    n = parse_equality();

    while (peek()->kind == TOK_AMP) {
        advance();
        n = new_binary(ND_BITAND, n, parse_equality());
        add_type(n);
        n = constant_fold(n);
    }
    return n;
}

/* bitwise xor */
static struct node *parse_bitxor(void)
{
    struct node *n;

    n = parse_bitand();

    while (peek()->kind == TOK_CARET) {
        advance();
        n = new_binary(ND_BITXOR, n, parse_bitand());
        add_type(n);
        n = constant_fold(n);
    }
    return n;
}

/* bitwise or */
static struct node *parse_bitor(void)
{
    struct node *n;

    n = parse_bitxor();

    while (peek()->kind == TOK_PIPE) {
        advance();
        n = new_binary(ND_BITOR, n, parse_bitxor());
        add_type(n);
        n = constant_fold(n);
    }
    return n;
}

/* logical and */
static struct node *parse_logand(void)
{
    struct node *n;

    n = parse_bitor();

    while (peek()->kind == TOK_AND) {
        advance();
        n = new_binary(ND_LOGAND, n, parse_bitor());
        add_type(n);
        n = constant_fold(n);
    }
    return n;
}

/* logical or */
static struct node *parse_logor(void)
{
    struct node *n;

    n = parse_logand();

    while (peek()->kind == TOK_OR) {
        advance();
        n = new_binary(ND_LOGOR, n, parse_logand());
        add_type(n);
        n = constant_fold(n);
    }
    return n;
}

/* ternary / conditional */
static struct node *parse_ternary(void)
{
    struct node *n;
    struct node *then_expr;
    struct node *else_expr;
    struct node *tern;

    n = parse_logor();

    if (peek()->kind == TOK_QUESTION) {
        advance();
        /* GNU extension: omitted operand ternary (a ?: b)
         * means (a ? a : b) — use condition as then-value */
        if (peek()->kind == TOK_COLON) {
            then_expr = n;
        } else {
            then_expr = parse_expr();
        }
        expect(TOK_COLON, "':'");
        else_expr = parse_ternary();

        tern = new_node(ND_TERNARY);
        tern->cond = n;
        tern->then_ = then_expr;
        tern->els = else_expr;
        add_type(then_expr);
        add_type(else_expr);
        /* Apply usual arithmetic conversions between branches.
         * For struct/union/pointer, keep the then type. */
        if (then_expr->ty != NULL && else_expr->ty != NULL &&
            then_expr->ty->kind != TY_STRUCT &&
            then_expr->ty->kind != TY_UNION &&
            !type_is_pointer(then_expr->ty) &&
            !type_is_pointer(else_expr->ty)) {
            tern->ty = get_common_type(then_expr, else_expr);
        } else {
            tern->ty = then_expr->ty;
        }
        n = constant_fold(tern);
    }

    return n;
}

/* assignment */
static struct node *parse_assign(void)
{
    struct node *n;
    struct node *rhs;
    enum tok_kind op;
    enum node_kind nk;

    n = parse_ternary();

    op = peek()->kind;

    if (op == TOK_ASSIGN) {
        advance();
        rhs = parse_assign();
        n = new_binary(ND_ASSIGN, n, rhs);
        add_type(n);
        return n;
    }

    /* compound assignment operators */
    if (op == TOK_PLUS_EQ || op == TOK_MINUS_EQ || op == TOK_STAR_EQ
        || op == TOK_SLASH_EQ || op == TOK_PERCENT_EQ
        || op == TOK_AMP_EQ || op == TOK_PIPE_EQ || op == TOK_CARET_EQ
        || op == TOK_LSHIFT_EQ || op == TOK_RSHIFT_EQ) {

        advance();
        rhs = parse_assign();

        switch (op) {
        case TOK_PLUS_EQ:    nk = ND_ADD; break;
        case TOK_MINUS_EQ:   nk = ND_SUB; break;
        case TOK_STAR_EQ:    nk = ND_MUL; break;
        case TOK_SLASH_EQ:   nk = ND_DIV; break;
        case TOK_PERCENT_EQ: nk = ND_MOD; break;
        case TOK_AMP_EQ:     nk = ND_BITAND; break;
        case TOK_PIPE_EQ:    nk = ND_BITOR; break;
        case TOK_CARET_EQ:   nk = ND_BITXOR; break;
        case TOK_LSHIFT_EQ:  nk = ND_SHL; break;
        case TOK_RSHIFT_EQ:  nk = ND_SHR; break;
        default:             nk = ND_ADD; break;
        }

        /* a op= b  =>  a = a op b */
        n = new_binary(ND_ASSIGN, n, new_binary(nk, n, rhs));
        add_type(n);
        return n;
    }

    return n;
}

/* comma expression */
static struct node *parse_expr(void)
{
    struct node *n;
    struct node *rhs;

    n = parse_assign();

    while (peek()->kind == TOK_COMMA) {
        advance();
        rhs = parse_assign();
        n = new_binary(ND_COMMA_EXPR, n, rhs);
        add_type(n);
    }

    return n;
}

static int type_is_aggregate_init(struct type *ty)
{
    if (ty == NULL) {
        return 0;
    }
    return ty->kind == TY_STRUCT ||
           ty->kind == TY_UNION ||
           ty->kind == TY_ARRAY;
}

static void append_local_init_assign(struct node **cur_decl,
                                     struct node *lhs,
                                     struct node *rhs)
{
    struct node *assign;

    assign = new_binary(ND_ASSIGN, lhs, rhs);
    add_type(assign);
    (*cur_decl)->next = assign;
    *cur_decl = assign;
    (*cur_decl)->next = NULL;
}

static struct node *parse_local_init_for_type(struct type *expect_ty);

static struct node *parse_next_brace_elided_init(struct type *expect_ty)
{
    struct node *init_expr;

    if (peek()->kind != TOK_COMMA) {
        return NULL;
    }
    advance();
    if (peek()->kind == TOK_RBRACE) {
        return NULL;
    }
    if (peek()->kind == TOK_DOT ||
        peek()->kind == TOK_LBRACKET) {
        return NULL;
    }
    if (peek()->kind == TOK_LBRACE) {
        init_expr = parse_global_init(expect_ty);
    } else if (expect_ty != NULL &&
               type_is_aggregate_init(expect_ty)) {
        /*
         * Brace-elided nested aggregates still need recursive parsing.
         * This lets members such as struct fields and sub-arrays consume
         * their full initializer sequence instead of only the first scalar.
         */
        init_expr = parse_local_init_for_type(expect_ty);
    } else {
        init_expr = parse_assign();
    }
    add_type(init_expr);
    return init_expr;
}

static struct node *parse_local_init_for_type(struct type *expect_ty)
{
    struct node *init_expr;

    if (expect_ty == NULL) {
        init_expr = parse_assign();
        add_type(init_expr);
        return init_expr;
    }

    if (peek()->kind == TOK_LBRACE) {
        return parse_global_init(expect_ty);
    }

    if (!type_is_aggregate_init(expect_ty)) {
        init_expr = parse_assign();
        add_type(init_expr);
        return init_expr;
    }

    if (expect_ty->kind == TY_STRUCT || expect_ty->kind == TY_UNION) {
        struct node *ilist;
        struct node head;
        struct node *tail;
        struct member *mbr;

        init_expr = parse_assign();
        add_type(init_expr);

        /* Whole-object init (struct variable or compound literal). */
        if (init_expr->kind != ND_INIT_LIST &&
            init_expr->ty != NULL &&
            type_is_aggregate_init(init_expr->ty) &&
            type_is_compatible(expect_ty, init_expr->ty)) {
            return init_expr;
        }

        ilist = new_node(ND_INIT_LIST);
        ilist->ty = expect_ty;
        head.next = NULL;
        tail = &head;

        init_expr->next = NULL;
        tail->next = init_expr;
        tail = init_expr;

        mbr = expect_ty->members;
        if (mbr == NULL) {
            ilist->body = head.next;
            return ilist;
        }
        if (expect_ty->kind == TY_UNION) {
            ilist->body = head.next;
            return ilist;
        }

        mbr = mbr->next;
        while (mbr != NULL) {
            struct node *v;
            v = parse_next_brace_elided_init(mbr->ty);
            if (v == NULL) {
                break;
            }
            v->next = NULL;
            tail->next = v;
            tail = v;
            mbr = mbr->next;
        }
        ilist->body = head.next;
        return ilist;
    }

    if (expect_ty->kind == TY_ARRAY && expect_ty->base != NULL &&
        expect_ty->array_len > 0) {
        struct node *ilist;
        struct node head;
        struct node *tail;
        int idx;

        init_expr = parse_assign();
        add_type(init_expr);

        /* array = string literal */
        if (init_expr->kind == ND_STR &&
            string_literal_matches_type(expect_ty, init_expr)) {
            return init_expr;
        }

        ilist = new_node(ND_INIT_LIST);
        ilist->ty = expect_ty;
        head.next = NULL;
        tail = &head;

        init_expr->next = NULL;
        tail->next = init_expr;
        tail = init_expr;

        idx = 1;
        while (idx < expect_ty->array_len) {
            struct node *v;
            v = parse_next_brace_elided_init(expect_ty->base);
            if (v == NULL) {
                break;
            }
            v->next = NULL;
            tail->next = v;
            tail = v;
            idx++;
        }
        ilist->body = head.next;
        return ilist;
    }

    /* Fallback: single initializer expression. */
    init_expr = parse_assign();
    add_type(init_expr);
    return init_expr;
}

static struct node *make_array_elem_lhs(struct node *base, int idx)
{
    struct node *idx_node;
    struct node *elem_addr;

    idx_node = new_num((long)idx);
    add_type(idx_node);
    add_type(base);
    elem_addr = new_unary(ND_DEREF,
        new_binary(ND_ADD, base, idx_node));
    add_type(elem_addr);
    return elem_addr;
}

static struct node *make_member_lhs(struct node *base,
                                    struct member *mbr)
{
    struct node *mem;

    mem = new_node(ND_MEMBER);
    mem->lhs = base;
    mem->name = mbr->name;
    mem->ty = mbr->ty;
    mem->offset = mbr->offset;
    mem->bit_width = mbr->bit_width;
    mem->bit_offset = mbr->bit_offset;
    return mem;
}

static int expr_has_side_effects(struct node *n)
{
    struct node *c;

    if (n == NULL) {
        return 0;
    }

    switch (n->kind) {
    case ND_ASSIGN:
    case ND_CALL:
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
    case ND_GCC_ASM:
    case ND_VA_START:
    case ND_VA_ARG:
        return 1;
    default:
        break;
    }

    if (expr_has_side_effects(n->lhs) ||
        expr_has_side_effects(n->rhs) ||
        expr_has_side_effects(n->cond) ||
        expr_has_side_effects(n->then_) ||
        expr_has_side_effects(n->els) ||
        expr_has_side_effects(n->init) ||
        expr_has_side_effects(n->inc) ||
        expr_has_side_effects(n->args)) {
        return 1;
    }

    for (c = n->body; c != NULL; c = c->next) {
        if (expr_has_side_effects(c)) {
            return 1;
        }
    }

    return 0;
}

static struct node *append_local_eval_temp(struct node *cur_decl,
                                           struct node *expr,
                                           struct node **temp_out)
{
    struct symbol *tmp_sym;
    struct node *tmp_var;
    struct node *tmp_assign;
    char tmp_name[64];

    if (cur_decl == NULL || expr == NULL || expr->ty == NULL) {
        if (temp_out != NULL) {
            *temp_out = expr;
        }
        return cur_decl;
    }

    sprintf(tmp_name, ".Ltmp.%d", label_counter++);
    tmp_sym = add_local(tmp_name, expr->ty);

    tmp_var = new_node(ND_VAR);
    tmp_var->name = tmp_sym->name;
    tmp_var->ty = tmp_sym->ty;
    tmp_var->offset = tmp_sym->offset;
    add_type(tmp_var);

    tmp_assign = new_binary(ND_ASSIGN, tmp_var, expr);
    add_type(tmp_assign);
    cur_decl->next = tmp_assign;
    tmp_assign->next = NULL;

    if (temp_out != NULL) {
        *temp_out = tmp_var;
    }
    return tmp_assign;
}

static void emit_local_aggregate_init(struct node **cur_decl,
                                      struct node *lhs,
                                      struct type *ty,
                                      struct node *init_expr)
{
    struct node *elem;
    struct node *val;
    struct member *mbr;

    if (lhs == NULL || ty == NULL || init_expr == NULL) {
        return;
    }

    if (!type_is_aggregate_init(ty)) {
        append_local_init_assign(cur_decl, lhs, init_expr);
        return;
    }

    /* Aggregate expression value with compatible type
     * (e.g. struct variable or compound literal) initializes
     * the whole object directly. */
    if (init_expr->kind != ND_INIT_LIST &&
        init_expr->ty != NULL &&
        type_is_aggregate_init(init_expr->ty) &&
        type_is_compatible(ty, init_expr->ty)) {
        append_local_init_assign(cur_decl, lhs, init_expr);
        return;
    }

    if (ty->kind == TY_ARRAY && ty->base != NULL) {
        int idx;
        struct node *elem_lhs;

        if (init_expr->kind == ND_STR &&
            string_literal_matches_type(ty, init_expr)) {
            append_local_init_assign(cur_decl, lhs, init_expr);
            return;
        }

        if (init_expr->kind == ND_INIT_LIST) {
            idx = 0;
            for (elem = init_expr->body;
                 elem != NULL;
                 elem = elem->next) {
                int use_idx;

                use_idx = idx;
                val = elem;
                if (elem->kind == ND_DESIG_INIT) {
                    use_idx = (int)elem->val;
                    if (use_idx < 0 || use_idx >= ty->array_len) {
                        continue;
                    }
                    if (use_idx >= idx) {
                        idx = use_idx + 1;
                    }
                    val = elem->lhs;
                } else {
                    if (idx >= ty->array_len) {
                        break;
                    }
                    idx++;
                }
                if (val == NULL) {
                    continue;
                }
                elem_lhs = make_array_elem_lhs(lhs, use_idx);
                emit_local_aggregate_init(cur_decl, elem_lhs,
                                          ty->base, val);
            }
            return;
        }

        idx = 0;
        val = init_expr;
        while (idx < ty->array_len && val != NULL) {
            elem_lhs = make_array_elem_lhs(lhs, idx);
            emit_local_aggregate_init(cur_decl, elem_lhs,
                                      ty->base, val);
            idx++;
            if (idx >= ty->array_len) {
                break;
            }
            val = parse_next_brace_elided_init(ty->base);
        }
        return;
    }

    mbr = ty->members;
    if (mbr == NULL) {
        return;
    }

    if (init_expr->kind == ND_INIT_LIST) {
        for (elem = init_expr->body;
             elem != NULL && mbr != NULL;
             elem = elem->next, mbr = mbr->next) {
            struct node *m_lhs;

            val = (elem->kind == ND_DESIG_INIT)
                ? elem->lhs : elem;
            if (val == NULL) {
                continue;
            }
            m_lhs = make_member_lhs(lhs, mbr);
            emit_local_aggregate_init(cur_decl, m_lhs,
                                      mbr->ty, val);
            if (ty->kind == TY_UNION) {
                break;
            }
        }
        return;
    }

    val = init_expr;
    while (mbr != NULL && val != NULL) {
        struct node *m_lhs;

        m_lhs = make_member_lhs(lhs, mbr);
        emit_local_aggregate_init(cur_decl, m_lhs,
                                  mbr->ty, val);
        if (ty->kind == TY_UNION) {
            break;
        }
        mbr = mbr->next;
        if (mbr == NULL) {
            break;
        }
        val = parse_next_brace_elided_init(mbr->ty);
    }
}

/* ---- declaration parsing (in block scope) ---- */

static struct node *parse_declaration(void)
{
    struct type *base_ty;
    struct type *ty;
    struct node head;
    struct node *cur_decl;
    struct node *assign;
    int is_td;
    int is_ext;
    int is_stat;
    char *name;
    struct symbol *sym;
    struct node *var;
    struct node *init_expr;

    head.next = NULL;
    cur_decl = &head;

    base_ty = parse_declspec(&is_td, &is_ext, &is_stat);

    /* If the base type is a VLA struct/union/array, snapshot the VLA
     * dimension into a hidden local so sizeof evaluates correctly
     * even after the dimension variable is modified. */
    if (base_ty != NULL && base_ty->is_vla && base_ty->vla_expr != NULL &&
        current_func_name != NULL) {
        char vla_name[64];
        struct symbol *vla_sym;
        struct node *vla_var;
        struct node *vla_assign;

        sprintf(vla_name, ".Lvla_sz.%d", label_counter++);
        vla_sym = add_local(vla_name, ty_long);
        vla_var = new_node(ND_VAR);
        vla_var->name = str_dup(parse_arena, vla_name,
                                (int)strlen(vla_name));
        vla_var->ty = ty_long;
        vla_var->offset = vla_sym->offset;

        vla_assign = new_binary(ND_ASSIGN, vla_var, base_ty->vla_expr);
        vla_assign->ty = ty_long;

        /* replace vla_expr with the hidden local */
        base_ty->vla_expr = new_node(ND_VAR);
        base_ty->vla_expr->name = vla_var->name;
        base_ty->vla_expr->ty = ty_long;
        base_ty->vla_expr->offset = vla_sym->offset;

        /* emit the save assignment as a statement */
        if (peek()->kind == TOK_SEMI) {
            advance();
            return vla_assign;
        }
        /* If not a standalone declaration, we still need the save.
         * Prepend it to the declarations. */
        head.next = NULL;
        cur_decl = &head;
        cur_decl->next = vla_assign;
        cur_decl = vla_assign;
        vla_assign->next = NULL;
        goto continue_declarator;
    }

    if (peek()->kind == TOK_SEMI) {
        advance();
        return NULL;
    }
continue_declarator:

    for (;;) {
        name = NULL;
        ty = parse_declarator(base_ty, &name);

        /* GCC nested function definition: parse body and emit as
         * a top-level function so the linker can resolve calls. */
        if (ty && ty->kind == TY_FUNC && peek()->kind == TOK_LBRACE) {
            if (name) {
                /* save enclosing function state */
                struct type *save_ret = current_ret_type;
                const char *save_fname = current_func_name;
                int save_offset = local_offset;
                int save_brk = brk_label;
                int save_cont = cont_label;
                struct node *save_switch = cur_switch;
                struct node *nfn;
                struct node arg_head2;
                struct node *arg_tail2;
                struct type *np;
                struct symbol *nsym;
                char mangled[256];
                char *mname;

                /* mangle nested function name to avoid collisions
                 * when multiple enclosing functions define nested
                 * functions with the same name */
                sprintf(mangled, ".Lnested.%s.%d.%s",
                    current_func_name ? current_func_name : "_",
                    label_counter++, name);
                mname = str_dup(parse_arena, mangled,
                    (int)strlen(mangled));

                /* register as a local symbol so calls resolve */
                nsym = (struct symbol *)arena_alloc(parse_arena,
                    sizeof(struct symbol));
                memset(nsym, 0, sizeof(struct symbol));
                nsym->name = str_dup(parse_arena, name,
                    (int)strlen(name));
                nsym->ty = ty;
                nsym->is_local = 1;
                nsym->is_defined = 1;
                nsym->asm_label = mname;
                nsym->next = cur_scope->locals;
                cur_scope->locals = nsym;

                /* also register as global so linker finds it */
                add_global(mname, ty)->is_defined = 1;

                /* build ND_FUNCDEF for the nested function */
                nfn = new_node(ND_FUNCDEF);
                nfn->name = mname;
                nfn->ty = ty;
                nfn->is_static = 0;

                /* set up nested function context.
                 * n_enclosing_labels is NOT reset here because the
                 * labels declared in the outer scope via __label__
                 * should remain available to the nested function. */
                current_ret_type = ty->ret;
                enclosing_func_name = current_func_name;
                current_func_name = name;
                local_offset = 0;
                brk_label = 0;
                cont_label = 0;
                cur_switch = NULL;

                enter_scope();
                cur_scope->is_func_boundary = 1;

                /* add parameters as locals */
                arg_head2.next = NULL;
                arg_tail2 = &arg_head2;
                {
                int nest_named_gp, nest_named_fp;
                nest_named_gp = 0;
                nest_named_fp = 0;
                np = ty->params;
                while (np != NULL) {
                    if (np->name) {
                        struct symbol *psym;
                        struct node *pn;
                        psym = add_local(np->name, np);
                        pn = new_node(ND_VAR);
                        pn->name = psym->name;
                        pn->ty = psym->ty;
                        pn->offset = psym->offset;
                        pn->next = NULL;
                        arg_tail2->next = pn;
                        arg_tail2 = pn;
                    }
                    if (np->kind == TY_FLOAT ||
                        np->kind == TY_DOUBLE) {
                        nest_named_fp++;
                    } else {
                        nest_named_gp++;
                    }
                    np = np->next;
                }
                nfn->args = arg_head2.next;

                /* variadic nested function: reserve register
                 * save areas just like top-level functions */
                if (ty->is_variadic) {
                    int nest_stack_bytes;
                    local_offset += 64;
                    local_offset = (local_offset + 15) & ~15;
                    nfn->va_save_offset = local_offset;
                    nfn->va_named_gp = nest_named_gp;
                    local_offset += 128;
                    local_offset = (local_offset + 15) & ~15;
                    nfn->va_fp_save_offset = local_offset;
                    nfn->va_named_fp = nest_named_fp;
                    nest_stack_bytes = 0;
                    if (nest_named_gp > 8) {
                        nest_stack_bytes +=
                            (nest_named_gp - 8) * 8;
                    }
                    if (nest_named_fp > 8) {
                        nest_stack_bytes +=
                            (nest_named_fp - 8) * 8;
                    }
                    nfn->va_stack_start =
                        16 + nest_stack_bytes;
                }
                }

                nfn->body = parse_compound_stmt();
                nfn->offset = local_offset;

                leave_scope();

                /* restore enclosing function state */
                current_ret_type = save_ret;
                current_func_name = save_fname;
                enclosing_func_name = NULL;
                local_offset = save_offset;
                brk_label = save_brk;
                cont_label = save_cont;
                cur_switch = save_switch;

                /* append to deferred nested function list */
                nfn->next = NULL;
                if (nested_funcs_tail) {
                    nested_funcs_tail->next = nfn;
                } else {
                    nested_funcs = nfn;
                }
                nested_funcs_tail = nfn;
            } else {
                skip_braces();
            }
            /* nested function is not followed by ';' or ',' */
            return head.next;
        }

        if (is_td) {
            /* apply __attribute__ parsed by parse_declarator to type */
            if (declarator_attrs.aligned > 0 || declarator_attrs.flags ||
                declarator_attrs.vector_size > 0) {
                attr_apply_to_type(ty, &declarator_attrs);
            }
            if (name) {
                add_typedef(name, ty);
            }
        } else if (is_stat && name != NULL) {
            /* static local: emit as global with mangled name */
            {
                char mangled[256];
                struct node *sn;
                struct symbol *lsym;

                sprintf(mangled, ".Lstatic.%d.%s",
                        static_local_counter++, name);

                sn = new_node(ND_VAR);
                sn->name = str_dup(parse_arena, mangled,
                                   (int)strlen(mangled));
                sn->ty = ty;
                sn->offset = 0;
                sn->is_static = 1;
                sn->val = 0;
                sn->next = NULL;

                if (peek()->kind == TOK_ASSIGN) {
                    struct node *gi;
                    advance();
                    gi = parse_global_init(ty);
                    /* array = string literal: size the array */
                    if (gi != NULL && gi->kind == ND_STR &&
                        string_literal_matches_type(ty, gi)) {
                        int slen;
                        slen = (int)gi->val + gi->ty->base->size;
                        if (ty->array_len == 0 ||
                            ty->size == 0) {
                            set_inferred_array_len(ty, slen);
                        }
                        sn->ty = ty;
                    }
                    /* unsized array: count init elements */
                    if (gi != NULL && gi->kind == ND_INIT_LIST &&
                        ty->kind == TY_ARRAY && ty->base &&
                        (ty->array_len == 0 || ty->size == 0)) {
                        int cnt;

                        cnt = infer_unsized_array_len(ty, gi);
                        set_inferred_array_len(ty, cnt);
                        sn->ty = ty;
                    }
                    if (gi != NULL && gi->kind == ND_NUM) {
                        sn->val = gi->val;
                    } else if (gi != NULL) {
                        sn->init = gi;
                    }
                }

                if (static_locals_tail) {
                    static_locals_tail->next = sn;
                } else {
                    static_locals = sn;
                }
                static_locals_tail = sn;

                lsym = (struct symbol *)arena_alloc(parse_arena,
                    sizeof(struct symbol));
                memset(lsym, 0, sizeof(struct symbol));
                lsym->name = str_dup(parse_arena, name,
                    (int)strlen(name));
                lsym->ty = ty;
                lsym->is_local = 0;
                lsym->is_defined = 1;
                lsym->offset = 0;
                lsym->asm_label = sn->name;
                lsym->next = cur_scope->locals;
                cur_scope->locals = lsym;
            }
        } else if (is_ext && name != NULL) {
            /* extern declaration inside function body:
             * don't allocate stack space, just add a local symbol
             * that references the global. */
            {
                struct symbol *esym;
                esym = (struct symbol *)arena_alloc(parse_arena,
                    sizeof(struct symbol));
                memset(esym, 0, sizeof(struct symbol));
                esym->name = str_dup(parse_arena, name,
                    (int)strlen(name));
                esym->ty = ty;
                esym->is_local = 0;
                esym->is_defined = 0;
                esym->offset = 0;
                esym->next = cur_scope->locals;
                cur_scope->locals = esym;
            }
        } else if (name != NULL) {
            /*
             * For unsized arrays (int arr[] = {...}), parse the init
             * list first to learn the element count, fix up the type,
             * then allocate the local so it gets a correct stack offset.
             */
            if (ty->kind == TY_ARRAY && ty->array_len == 0 &&
                peek()->kind == TOK_ASSIGN) {
#define MAX_INIT_ELEMS 256
                struct node *init_exprs[MAX_INIT_ELEMS];
                int init_count;
                int idx;
                struct node *idx_node;
                struct node *elem_addr;
                struct node *elem_assign;

                /* Unsized array typedefs are shared type objects.
                 * Copy before inferring the length so sibling
                 * declarators do not inherit the mutated size. */
                ty = copy_type(ty);

                advance(); /* consume '=' */

                /* handle string literal initializer for arrays whose
                 * element width matches the string literal width. */
                if (peek()->kind == TOK_STR && ty->base &&
                    (ty->base->size == 1 ||
                     ty->base->size == 2 ||
                     ty->base->size == 4)) {
                    init_expr = parse_assign();
                    add_type(init_expr);
                    /* Set array length from the parsed string literal.
                     * ND_STR.ty->array_len includes the trailing NUL. */
                    if (init_expr->kind == ND_STR) {
                        set_inferred_array_len(ty,
                            init_expr->ty->array_len);
                    } else {
                        set_inferred_array_len(ty, 1);
                    }
                    sym = add_local(name, ty);

                    var = new_node(ND_VAR);
                    var->name = sym->name;
                    var->ty = sym->ty;
                    var->offset = sym->offset;
                    add_type(var);
                    assign = new_binary(ND_ASSIGN, var,
                                        init_expr);
                    add_type(assign);
                    cur_decl->next = assign;
                    cur_decl = assign;
                    cur_decl->next = NULL;

                    goto next_decl;
                }

                expect(TOK_LBRACE, "'{'");
                init_count = 0;
                {
                    /* track actual indices for designated inits
                     * in unsized arrays */
                    int init_indices[MAX_INIT_ELEMS];
                    int cur_idx;
                    int max_idx;
                    int range_start;
                    int range_hi;
                    int ri;
                    cur_idx = 0;
                    max_idx = -1;
                    while (peek()->kind != TOK_RBRACE &&
                           peek()->kind != TOK_EOF) {
                        range_start = cur_idx;
                        range_hi = cur_idx;
                        /* [index] = value */
                        if (peek()->kind == TOK_LBRACKET &&
                            cc_has_feat(FEAT_DESIG_INIT)) {
                            struct node *di2;
                            advance(); /* '[' */
                            di2 = parse_assign();
                            add_type(di2);
                            di2 = constant_fold(di2);
                            if (di2->kind == ND_NUM) {
                                cur_idx = (int)di2->val;
                            }
                            range_start = cur_idx;
                            range_hi = cur_idx;
                            if (peek()->kind == TOK_ELLIPSIS) {
                                struct node *hi2;
                                advance();
                                hi2 = parse_assign();
                                add_type(hi2);
                                hi2 = constant_fold(hi2);
                                if (hi2->kind == ND_NUM) {
                                    range_hi = (int)hi2->val;
                                }
                            }
                            expect(TOK_RBRACKET, "']'");
                            expect(TOK_ASSIGN, "'='");
                        }
                        if (peek()->kind == TOK_LBRACE) {
                            /* nested brace initializer for
                             * array element (e.g. struct) */
                            init_expr = parse_global_init(
                                ty->base);
                        } else {
                            if (ty->base != NULL &&
                                type_is_aggregate_init(ty->base)) {
                                init_expr = parse_local_init_for_type(
                                    ty->base);
                            } else {
                                init_expr = parse_assign();
                                add_type(init_expr);
                            }
                        }
                        if (range_hi > range_start &&
                            init_expr != NULL &&
                            init_expr->ty != NULL &&
                            !type_is_aggregate_init(
                                init_expr->ty) &&
                            expr_has_side_effects(init_expr)) {
                            struct node *tmp_expr;
                            cur_decl = append_local_eval_temp(
                                cur_decl, init_expr, &tmp_expr);
                            init_expr = tmp_expr;
                        }
                        if (range_hi < range_start) {
                            range_start = cur_idx;
                            range_hi = cur_idx;
                        }
                        for (ri = range_start; ri <= range_hi; ri++) {
                            if (init_count < MAX_INIT_ELEMS) {
                                init_exprs[init_count] =
                                    init_expr;
                                init_indices[init_count] = ri;
                            }
                            if (ri > max_idx) {
                                max_idx = ri;
                            }
                            init_count++;
                        }
                        cur_idx = range_hi + 1;
                        if (peek()->kind == TOK_COMMA) {
                            advance();
                        } else {
                            break;
                        }
                    }
                    expect(TOK_RBRACE, "'}'");

                    /* fix up array type before allocating */
                    if (max_idx >= 0) {
                        set_inferred_array_len(ty, max_idx + 1);
                    }

                    sym = add_local(name, ty);

                    /* zero-fill the array so unspecified elements
                     * become 0 even for automatic locals */
                    {
                        struct node *zvar;
                        struct node *zilist;
                        struct node *zassign;

                        zvar = new_node(ND_VAR);
                        zvar->name = sym->name;
                        zvar->ty = sym->ty;
                        zvar->offset = sym->offset;
                        zilist = new_node(ND_INIT_LIST);
                        zilist->ty = ty;
                        zilist->body = NULL;
                        zassign = new_binary(ND_ASSIGN, zvar, zilist);
                        add_type(zassign);
                        cur_decl->next = zassign;
                        cur_decl = zassign;
                        cur_decl->next = NULL;
                    }

                    /* emit element assignments */
                    for (idx = 0; idx < init_count &&
                                  idx < MAX_INIT_ELEMS;
                         idx++) {
                        if (ty->base != NULL &&
                            type_is_aggregate_init(ty->base)) {
                            struct node *av;
                            struct node *ai;
                            struct node *ae;

                            av = new_node(ND_VAR);
                            av->name = sym->name;
                            av->ty = sym->ty;
                            av->offset = sym->offset;
                            ai = new_num(
                                (long)init_indices[idx]);
                            ae = new_unary(ND_DEREF,
                                new_binary(ND_ADD, av, ai));
                            add_type(av);
                            add_type(ae);
                            emit_local_aggregate_init(
                                &cur_decl, ae, ty->base,
                                init_exprs[idx]);
                        } else {
                            var = new_node(ND_VAR);
                            var->name = sym->name;
                            var->ty = sym->ty;
                            var->offset = sym->offset;

                            idx_node = new_num(
                                (long)init_indices[idx]);
                            elem_addr = new_unary(ND_DEREF,
                                new_binary(ND_ADD, var,
                                           idx_node));
                            add_type(var);
                            add_type(elem_addr);
                            elem_assign = new_binary(
                                ND_ASSIGN, elem_addr,
                                init_exprs[idx]);
                            add_type(elem_assign);

                            cur_decl->next = elem_assign;
                            cur_decl = elem_assign;
                            cur_decl->next = NULL;
                        }
                    }
                }
#undef MAX_INIT_ELEMS
            } else {
                /* __auto_type: infer type from initializer */
                if (ty == &ty_auto_type_sentinel
                    && peek()->kind == TOK_ASSIGN) {
                    struct node *auto_init;
                    advance(); /* consume '=' */
                    auto_init = parse_assign();
                    add_type(auto_init);
                    ty = auto_init->ty ? auto_init->ty : ty_int;
                    sym = add_local(name, ty);
                    var = new_node(ND_VAR);
                    var->name = sym->name;
                    var->ty = sym->ty;
                    var->offset = sym->offset;
                    add_type(var);
                    assign = new_binary(ND_ASSIGN, var,
                                        auto_init);
                    add_type(assign);
                    cur_decl->next = assign;
                    cur_decl = assign;
                    cur_decl->next = NULL;
                    goto next_decl;
                }
                if (ty == &ty_auto_type_sentinel) {
                    ty = ty_int; /* fallback if no initializer */
                }

                /* sized array or non-array local variable */
                sym = add_local(name, ty);

                if (peek()->kind == TOK_ASSIGN) {
                    advance();

                    if (peek()->kind == TOK_LBRACE) {
                        /* initializer list { expr, expr, ... } */
                        int idx;
                        struct node *elem_var;
                        struct node *idx_node;
                        struct node *elem_addr;
                        struct node *elem_assign;
                        struct member *mbr;

                        advance(); /* consume '{' */

                        /* zero-fill the entire variable first
                         * so that uninitialized members are 0
                         * (C89 6.5.7: partial init zeroes rest) */
                        {
                            struct node *zvar;
                            struct node *zilist;
                            struct node *zassign;

                            zvar = new_node(ND_VAR);
                            zvar->name = sym->name;
                            zvar->ty = sym->ty;
                            zvar->offset = sym->offset;
                            zilist = new_node(ND_INIT_LIST);
                            zilist->ty = ty;
                            zilist->body = NULL;
                            zassign = new_binary(ND_ASSIGN,
                                                 zvar, zilist);
                            add_type(zassign);
                            cur_decl->next = zassign;
                            cur_decl = zassign;
                            cur_decl->next = NULL;
                        }

                        idx = 0;
                        mbr = NULL;
                        if (ty->kind == TY_STRUCT ||
                            ty->kind == TY_UNION) {
                            mbr = ty->members;
                        }
                        while (peek()->kind != TOK_RBRACE &&
                               peek()->kind != TOK_EOF) {

                            /* .field = value designator
                             * or .field1.field2 = value */
                            if (peek()->kind == TOK_DOT &&
                                cc_has_feat(FEAT_DESIG_INIT)) {
                                struct member *dm;
                                struct type *chain_ty;
                                int chain_off;
                                char *fn2;
                                int fl2;
                                advance(); /* '.' */
                                fn2 = (char *)peek()->str;
                                fl2 = peek()->len;
                                advance(); /* field */
                                dm = NULL;
                                chain_off = 0;
                                chain_ty = ty;
                                if (chain_ty != NULL) {
                                    dm = find_member_by_name(
                                        chain_ty,
                                        str_dup(parse_arena,
                                            fn2, fl2));
                                }
                                if (dm != NULL) {
                                    chain_off = dm->offset;
                                    chain_ty = dm->ty;
                                }
                                /* handle chained: .a.b.c */
                                while (peek()->kind == TOK_DOT) {
                                    advance(); /* '.' */
                                    fn2 = (char *)peek()->str;
                                    fl2 = peek()->len;
                                    advance();
                                    dm = NULL;
                                    if (chain_ty != NULL) {
                                        dm = find_member_by_name(
                                            chain_ty,
                                            str_dup(parse_arena,
                                                fn2, fl2));
                                    }
                                    if (dm != NULL) {
                                        chain_off += dm->offset;
                                        chain_ty = dm->ty;
                                    }
                                }
                                expect(TOK_ASSIGN, "'='");
                                if (peek()->kind == TOK_LBRACE) {
                                    init_expr =
                                        parse_global_init(
                                            dm ? dm->ty : NULL);
                                } else {
                                    init_expr = parse_assign();
                                    add_type(init_expr);
                                }
                                if (dm != NULL) {
                                    var = new_node(ND_VAR);
                                    var->name = sym->name;
                                    var->ty = sym->ty;
                                    var->offset = sym->offset;
                                    elem_var =
                                        new_node(ND_MEMBER);
                                    elem_var->lhs = var;
                                    elem_var->name = dm->name;
                                    elem_var->ty = dm->ty;
                                    elem_var->offset =
                                        chain_off;
                                    elem_var->bit_width =
                                        dm->bit_width;
                                    elem_var->bit_offset =
                                        dm->bit_offset;
                                    if (init_expr->kind ==
                                        ND_INIT_LIST &&
                                        dm->ty != NULL &&
                                        (dm->ty->kind ==
                                         TY_STRUCT ||
                                         dm->ty->kind ==
                                         TY_UNION)) {
                                        /* nested struct init:
                                         * emit member-by-member
                                         * assigns */
                                        struct node *sub;
                                        struct member *sm;
                                        struct node *sv;
                                        struct node *smn;
                                        struct node *sval;
                                        sm = dm->ty->members;
                                        for (sub =
                                             init_expr->body;
                                             sub != NULL;
                                             sub = sub->next) {
                                            /* get value and
                                             * target member */
                                            if (sub->kind ==
                                                ND_DESIG_INIT) {
                                                sm =
                                                find_member_by_name(
                                                    dm->ty,
                                                    sub->name);
                                                sval =
                                                    sub->lhs;
                                            } else {
                                                sval = sub;
                                            }
                                            if (sm == NULL)
                                                break;
                                            sv = new_node(
                                                ND_VAR);
                                            sv->name =
                                                sym->name;
                                            sv->ty = sym->ty;
                                            sv->offset =
                                                sym->offset;
                                            smn = new_node(
                                                ND_MEMBER);
                                            smn->lhs = sv;
                                            smn->name =
                                                dm->name;
                                            smn->ty = dm->ty;
                                            smn->offset =
                                                dm->offset;
                                            /* create nested
                                             * member access */
                                            elem_var =
                                                new_node(
                                                ND_MEMBER);
                                            elem_var->lhs =
                                                smn;
                                            elem_var->name =
                                                sm->name;
                                            elem_var->ty =
                                                sm->ty;
                                            elem_var->offset =
                                                sm->offset;
                                            elem_assign =
                                                new_binary(
                                                ND_ASSIGN,
                                                elem_var,
                                                sval);
                                            add_type(
                                                elem_assign);
                                            cur_decl->next =
                                                elem_assign;
                                            cur_decl =
                                                elem_assign;
                                            cur_decl->next =
                                                NULL;
                                            sm = sm->next;
                                        }
                                    } else {
                                        elem_assign =
                                            new_binary(
                                            ND_ASSIGN,
                                            elem_var,
                                            init_expr);
                                        add_type(elem_assign);
                                        cur_decl->next =
                                            elem_assign;
                                        cur_decl = elem_assign;
                                        cur_decl->next = NULL;
                                    }
                                    mbr = dm->next;
                                }
                                if (peek()->kind == TOK_COMMA) {
                                    advance();
                                } else {
                                    break;
                                }
                                continue;
                            }

                            /* [index] = value designator
                             * or [low ... high] = value */
                            if (peek()->kind == TOK_LBRACKET &&
                                cc_has_feat(FEAT_DESIG_INIT)) {
                                struct node *di_expr;
                                long di_val;
                                long di_hi;
                                long di_ri;
                                advance(); /* '[' */
                                di_expr = parse_assign();
                                add_type(di_expr);
                                di_expr = constant_fold(di_expr);
                                di_val = 0;
                                if (di_expr->kind == ND_NUM) {
                                    di_val = di_expr->val;
                                }
                                di_hi = di_val;
                                if (peek()->kind == TOK_ELLIPSIS) {
                                    struct node *hi_e;
                                    advance();
                                    hi_e = parse_assign();
                                    add_type(hi_e);
                                    hi_e = constant_fold(hi_e);
                                    if (hi_e->kind == ND_NUM) {
                                        di_hi = hi_e->val;
                                    }
                                }
                                expect(TOK_RBRACKET, "']'");
                                expect(TOK_ASSIGN, "'='");
                                init_expr = parse_assign();
                                add_type(init_expr);
                                if (di_hi > di_val &&
                                    init_expr != NULL &&
                                    init_expr->ty != NULL &&
                                    !type_is_aggregate_init(
                                        init_expr->ty) &&
                                    expr_has_side_effects(
                                        init_expr)) {
                                    struct node *tmp_expr;
                                    cur_decl = append_local_eval_temp(
                                        cur_decl, init_expr,
                                        &tmp_expr);
                                    init_expr = tmp_expr;
                                }

                                for (di_ri = di_val;
                                     di_ri <= di_hi; di_ri++) {
                                    var = new_node(ND_VAR);
                                    var->name = sym->name;
                                    var->ty = sym->ty;
                                    var->offset = sym->offset;

                                    if (ty->kind == TY_ARRAY &&
                                        ty->base) {
                                        idx_node =
                                            new_num(di_ri);
                                        elem_addr = new_unary(
                                            ND_DEREF,
                                            new_binary(ND_ADD,
                                                var, idx_node));
                                        add_type(var);
                                        add_type(elem_addr);
                                        elem_assign = new_binary(
                                            ND_ASSIGN, elem_addr,
                                            init_expr);
                                        add_type(elem_assign);
                                    } else {
                                        elem_assign = new_binary(
                                            ND_ASSIGN, var,
                                            init_expr);
                                        add_type(elem_assign);
                                    }
                                    cur_decl->next = elem_assign;
                                    cur_decl = elem_assign;
                                    cur_decl->next = NULL;
                                }
                                idx = (int)di_hi + 1;

                                if (peek()->kind == TOK_COMMA) {
                                    advance();
                                } else {
                                    break;
                                }
                                continue;
                            }

                            /* GNU field: value designator */
                            if (peek()->kind == TOK_IDENT &&
                                cc_has_feat(FEAT_DESIG_INIT) &&
                                (ty->kind == TY_STRUCT ||
                                 ty->kind == TY_UNION)) {
                                struct tok *id_tok2;
                                id_tok2 = advance();
                                if (peek()->kind == TOK_COLON) {
                                    struct member *dm2;
                                    advance();
                                    dm2 = find_member_by_name(
                                        ty,
                                        str_dup(parse_arena,
                                            id_tok2->str,
                                            id_tok2->len));
                                    if (peek()->kind ==
                                        TOK_LBRACE) {
                                        init_expr =
                                            parse_global_init(
                                            dm2 ? dm2->ty
                                                : NULL);
                                    } else {
                                        init_expr =
                                            parse_assign();
                                    }
                                    add_type(init_expr);
                                    if (dm2 != NULL) {
                                        var = new_node(ND_VAR);
                                        var->name = sym->name;
                                        var->ty = sym->ty;
                                        var->offset =
                                            sym->offset;
                                        elem_var =
                                            new_node(
                                            ND_MEMBER);
                                        elem_var->lhs = var;
                                        elem_var->name =
                                            dm2->name;
                                        elem_var->ty =
                                            dm2->ty;
                                        elem_var->offset =
                                            dm2->offset;
                                        elem_var->bit_width =
                                            dm2->bit_width;
                                        elem_var->bit_offset =
                                            dm2->bit_offset;
                                        elem_assign =
                                            new_binary(
                                            ND_ASSIGN,
                                            elem_var,
                                            init_expr);
                                        add_type(elem_assign);
                                        cur_decl->next =
                                            elem_assign;
                                        cur_decl =
                                            elem_assign;
                                        cur_decl->next = NULL;
                                        mbr = dm2->next;
                                    }
                                    if (peek()->kind ==
                                        TOK_COMMA) {
                                        advance();
                                    } else {
                                        break;
                                    }
                                    continue;
                                }
                                unadvance(id_tok2);
                            }

                            if (peek()->kind == TOK_LBRACE) {
                                /* nested brace initializer for
                                 * array element or struct member */
                                struct type *elem_ty;
                                elem_ty = NULL;
                                if (ty->kind == TY_ARRAY &&
                                    ty->base) {
                                    elem_ty = ty->base;
                                } else if (mbr != NULL) {
                                    elem_ty = mbr->ty;
                                }
                                init_expr =
                                    parse_global_init(elem_ty);
                            } else {
                                if (mbr != NULL && mbr->ty != NULL &&
                                    type_is_aggregate_init(mbr->ty)) {
                                    /*
                                     * Brace-elided aggregate members need
                                     * recursive parsing so nested structs
                                     * and arrays consume the whole member
                                     * initializer list instead of behaving
                                     * like a scalar assignment to the
                                     * first field only.
                                     */
                                    init_expr =
                                        parse_local_init_for_type(
                                            mbr->ty);
                                } else {
                                    init_expr = parse_assign();
                                }
                            }
                            add_type(init_expr);

                            var = new_node(ND_VAR);
                            var->name = sym->name;
                            var->ty = sym->ty;
                            var->offset = sym->offset;

                            if (ty->kind == TY_ARRAY &&
                                ty->base) {
                                if (ty->base != NULL &&
                                    type_is_aggregate_init(
                                        ty->base)) {
                                    struct node *av2;
                                    struct node *ai2;
                                    struct node *ae2;

                                    av2 = new_node(
                                        ND_VAR);
                                    av2->name =
                                        sym->name;
                                    av2->ty = sym->ty;
                                    av2->offset =
                                        sym->offset;
                                    ai2 = new_num(
                                        (long)idx);
                                    ae2 = new_unary(
                                        ND_DEREF,
                                        new_binary(
                                        ND_ADD,
                                        av2, ai2));
                                    add_type(av2);
                                    add_type(ae2);
                                    emit_local_aggregate_init(
                                        &cur_decl, ae2,
                                        ty->base,
                                        init_expr);
                                    goto skip_elem_assign;
                                }
                                /* arr[idx] = init_expr */
                                idx_node = new_num((long)idx);
                                elem_addr = new_unary(ND_DEREF,
                                    new_binary(ND_ADD, var,
                                               idx_node));
                                add_type(var);
                                add_type(elem_addr);
                                elem_assign = new_binary(
                                    ND_ASSIGN, elem_addr,
                                    init_expr);
                                add_type(elem_assign);
                            } else if (mbr != NULL &&
                                       mbr->ty != NULL &&
                                       mbr->ty->kind == TY_ARRAY &&
                                       mbr->ty->base != NULL) {
                                struct node *m_lhs;

                                m_lhs = make_member_lhs(var, mbr);
                                if (init_expr->kind == ND_STR &&
                                    string_literal_matches_type(
                                        mbr->ty, init_expr)) {
                                    elem_assign = new_binary(
                                        ND_ASSIGN, m_lhs, init_expr);
                                    add_type(elem_assign);
                                    cur_decl->next = elem_assign;
                                    cur_decl = elem_assign;
                                    cur_decl->next = NULL;
                                } else if (init_expr->kind == ND_INIT_LIST &&
                                           init_list_has_index_designator(
                                               init_expr)) {
                                    struct node *elem2;
                                    int next_idx;

                                    next_idx = 0;
                                    for (elem2 = init_expr->body;
                                         elem2 != NULL;
                                         elem2 = elem2->next) {
                                        int use_idx;
                                        struct node *val2;

                                        use_idx = next_idx;
                                        val2 = elem2;
                                        if (elem2->kind ==
                                            ND_DESIG_INIT) {
                                            use_idx = (int)elem2->val;
                                            val2 = elem2->lhs;
                                            if (use_idx >= next_idx) {
                                                next_idx = use_idx + 1;
                                            }
                                        } else {
                                            next_idx++;
                                        }
                                        if (val2 == NULL) {
                                            continue;
                                        }
                                        idx_node = new_num(
                                            (long)use_idx);
                                        elem_addr = new_unary(
                                            ND_DEREF,
                                            new_binary(ND_ADD,
                                                m_lhs, idx_node));
                                        add_type(elem_addr);
                                        elem_assign = new_binary(
                                            ND_ASSIGN, elem_addr,
                                            val2);
                                        add_type(elem_assign);
                                        cur_decl->next = elem_assign;
                                        cur_decl = elem_assign;
                                        cur_decl->next = NULL;
                                    }
                                } else {
                                    /*
                                     * Brace-elided array members need
                                     * element-wise expansion so scalars
                                     * like "struct S s = {1, 2, 3, 4}"
                                     * become c[0]=3, c[1]=4 instead of a
                                     * bogus aggregate copy from address 3.
                                     */
                                    emit_local_aggregate_init(
                                        &cur_decl, m_lhs,
                                        mbr->ty, init_expr);
                                }
                                mbr = mbr->next;
                                goto skip_elem_assign;
                            } else if (mbr != NULL &&
                                       mbr->ty != NULL &&
                                       (mbr->ty->kind == TY_STRUCT ||
                                        mbr->ty->kind == TY_UNION)) {
                                elem_var = new_node(ND_MEMBER);
                                elem_var->lhs = var;
                                elem_var->name = mbr->name;
                                elem_var->ty = mbr->ty;
                                elem_var->offset = mbr->offset;
                                elem_var->bit_width =
                                    mbr->bit_width;
                                elem_var->bit_offset =
                                    mbr->bit_offset;
                                elem_assign = new_binary(
                                    ND_ASSIGN, elem_var,
                                    init_expr);
                                add_type(elem_assign);
                                cur_decl->next = elem_assign;
                                cur_decl = elem_assign;
                                cur_decl->next = NULL;
                                mbr = mbr->next;
                                goto skip_elem_assign;
                            } else if (mbr != NULL) {
                                /* s.member = init_expr */
                                elem_var = new_node(ND_MEMBER);
                                elem_var->lhs = var;
                                elem_var->name = mbr->name;
                                elem_var->ty = mbr->ty;
                                elem_var->offset = mbr->offset;
                                elem_var->bit_width =
                                    mbr->bit_width;
                                elem_var->bit_offset =
                                    mbr->bit_offset;
                                elem_assign = new_binary(
                                    ND_ASSIGN, elem_var,
                                    init_expr);
                                add_type(elem_assign);
                                mbr = mbr->next;
                            } else {
                                elem_assign = new_binary(
                                    ND_ASSIGN, var, init_expr);
                                add_type(elem_assign);
                            }
                            cur_decl->next = elem_assign;
                            cur_decl = elem_assign;
                            cur_decl->next = NULL;
                        skip_elem_assign:
                            idx++;

                            if (peek()->kind == TOK_COMMA) {
                                advance();
                            } else {
                                break;
                            }
                        }
                        expect(TOK_RBRACE, "'}'");
                    } else {
                        init_expr = parse_assign();
                        add_type(init_expr);

                        var = new_node(ND_VAR);
                        var->name = sym->name;
                        var->ty = sym->ty;
                        var->offset = sym->offset;

                        assign = new_binary(ND_ASSIGN, var,
                                            init_expr);
                        add_type(assign);
                        cur_decl->next = assign;
                        cur_decl = assign;
                        cur_decl->next = NULL;
                    }
                }
            }
        }

        next_decl:
        if (peek()->kind != TOK_COMMA) {
            break;
        }
        advance(); /* consume ',' */
    }

    expect(TOK_SEMI, "';'");
    return head.next;
}

/* ---- helper: expression statement from consumed ident (label lookahead) ---- */

static struct node *parse_expr_stmt_from_ident(struct tok *saved)
{
    struct node *var_n;
    struct symbol *sym;
    long eval;
    struct node arg_head;
    struct node *ca;
    struct node *arg;
    struct tok *t;
    struct member *mem;
    struct node *idx;
    struct node *mn;
    struct node *rhs_node;
    enum tok_kind aop;
    enum node_kind ank;

    /* GNU __real__ / __imag__ as expression statements:
     * put back the token and let the expression parser handle it
     * through the unary operator path, so __real r = expr
     * is correctly parsed as (__real r) = expr */
    if (saved->str &&
        (strcmp(saved->str, "__real__") == 0 ||
         strcmp(saved->str, "__real") == 0 ||
         strcmp(saved->str, "__imag__") == 0 ||
         strcmp(saved->str, "__imag") == 0)) {
        struct node *stmt_n;
        unadvance(saved);
        stmt_n = parse_expr();
        add_type(stmt_n);
        expect(TOK_SEMI, "';'");
        return stmt_n;
    }

    /* variadic intrinsics as expression statements */
    if (saved->str && strcmp(saved->str, "__builtin_va_start") == 0) {
        struct node *ap_expr;

        expect(TOK_LPAREN, "'('");
        ap_expr = parse_assign();
        add_type(ap_expr);
        expect(TOK_COMMA, "','");
        parse_assign(); /* skip last named arg */
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");

        var_n = new_node(ND_VA_START);
        var_n->lhs = ap_expr;
        var_n->ty = ty_void;
        return var_n;
    }

    if (saved->str && strcmp(saved->str, "__builtin_va_end") == 0) {
        expect(TOK_LPAREN, "'('");
        parse_assign();
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");
        return new_num(0);
    }

    /* __builtin_va_arg(ap, type) as statement — evaluate and discard */
    if (saved->str && strcmp(saved->str, "__builtin_va_arg") == 0) {
        struct type *arg_ty;
        struct node *ap_expr;

        expect(TOK_LPAREN, "'('");
        ap_expr = parse_assign();
        add_type(ap_expr);
        expect(TOK_COMMA, "','");
        arg_ty = parse_type_name();
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");

        var_n = new_node(ND_VA_ARG);
        var_n->lhs = ap_expr;
        var_n->ty = arg_ty;
        return var_n;
    }

    /* __builtin_{add,sub,mul}_overflow as statement */
    if (saved->str &&
        (strcmp(saved->str, "__builtin_add_overflow") == 0 ||
         strcmp(saved->str, "__builtin_sub_overflow") == 0 ||
         strcmp(saved->str, "__builtin_mul_overflow") == 0)) {
        struct node *a_expr;
        struct node *b_expr;
        struct node *res_expr;
        int op;

        if (saved->str[10] == 'a') op = 0;
        else if (saved->str[10] == 's') op = 1;
        else op = 2;

        expect(TOK_LPAREN, "'('");
        a_expr = parse_assign();
        add_type(a_expr);
        expect(TOK_COMMA, "','");
        b_expr = parse_assign();
        add_type(b_expr);
        expect(TOK_COMMA, "','");
        res_expr = parse_assign();
        add_type(res_expr);
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");

        var_n = new_node(ND_BUILTIN_OVERFLOW);
        var_n->lhs = a_expr;
        var_n->rhs = b_expr;
        var_n->body = res_expr;
        var_n->val = op;
        var_n->ty = ty_int;
        return var_n;
    }

    /* __builtin_frame_address as statement */
    if (saved->str &&
        strcmp(saved->str, "__builtin_frame_address") == 0) {
        expect(TOK_LPAREN, "'('");
        parse_assign();
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");
        var_n = new_node(ND_CALL);
        var_n->name = str_dup(parse_arena,
            "__builtin_frame_address", 23);
        var_n->ty = type_ptr(ty_void);
        var_n->args = NULL;
        return var_n;
    }

    /* __builtin_return_address as statement */
    if (saved->str &&
        strcmp(saved->str, "__builtin_return_address") == 0) {
        struct node *depth_arg2;
        expect(TOK_LPAREN, "'('");
        depth_arg2 = parse_assign();
        add_type(depth_arg2);
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");
        var_n = new_node(ND_CALL);
        var_n->name = str_dup(parse_arena,
            "__builtin_return_address", 24);
        var_n->ty = type_ptr(ty_void);
        if (depth_arg2 && depth_arg2->kind == ND_NUM) {
            var_n->val = depth_arg2->val;
        }
        var_n->args = NULL;
        return var_n;
    }

    /* __builtin_types_compatible_p as statement */
    if (saved->str &&
        strcmp(saved->str, "__builtin_types_compatible_p") == 0) {
        struct type *ty1;
        struct type *ty2;
        int compat;
        expect(TOK_LPAREN, "'('");
        ty1 = parse_type_name();
        expect(TOK_COMMA, "','");
        ty2 = parse_type_name();
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");
        compat = (ty1 && ty2 && type_is_compatible(ty1, ty2)) ? 1 : 0;
        return new_num((long)compat);
    }

    /* __builtin_choose_expr as statement */
    if (saved->str &&
        strcmp(saved->str, "__builtin_choose_expr") == 0) {
        struct node *ce;
        struct node *e1;
        struct node *e2;
        expect(TOK_LPAREN, "'('");
        ce = parse_assign();
        add_type(ce);
        expect(TOK_COMMA, "','");
        e1 = parse_assign();
        add_type(e1);
        expect(TOK_COMMA, "','");
        e2 = parse_assign();
        add_type(e2);
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");
        if (ce->kind == ND_NUM && ce->val != 0) return e1;
        return e2;
    }

    /* __builtin_constant_p: handled by expression parser (parse_primary),
     * do NOT intercept here — it must participate in ternary exprs. */

    /* __builtin_expect: handled by expression parser (parse_primary),
     * do NOT intercept here — it must participate in ternary exprs.
     * Same for __builtin_expect_with_probability. */

    /* __builtin_offsetof as statement */
    if (saved->str &&
        strcmp(saved->str, "__builtin_offsetof") == 0) {
        struct type *oty;
        struct member *om;
        expect(TOK_LPAREN, "'('");
        oty = parse_type_name();
        expect(TOK_COMMA, "','");
        t = expect(TOK_IDENT, "member name");
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");
        if (oty && (oty->kind == TY_STRUCT || oty->kind == TY_UNION)) {
            om = type_find_member(oty, t->str);
            if (om) return new_num((long)om->offset);
        }
        return new_num(0);
    }

    /* __builtin_* -> libc name mapping for statement-level calls */
    if (saved->str &&
        strncmp(saved->str, "__builtin_", 10) == 0 &&
        peek()->kind == TOK_LPAREN) {
        const char *real_name;
        real_name = NULL;
        if (strcmp(saved->str, "__builtin_puts") == 0)
            real_name = "puts";
        else if (strcmp(saved->str, "__builtin_snprintf") == 0)
            real_name = "snprintf";
        else if (strcmp(saved->str, "__builtin_printf") == 0)
            real_name = "printf";
        else if (strcmp(saved->str, "__builtin_sprintf") == 0)
            real_name = "sprintf";
        else if (strcmp(saved->str, "__builtin_memcpy") == 0)
            real_name = "memcpy";
        else if (strcmp(saved->str, "__builtin_memset") == 0)
            real_name = "memset";
        else if (strcmp(saved->str, "__builtin_strcmp") == 0)
            real_name = "strcmp";
        else if (strcmp(saved->str, "__builtin_strlen") == 0)
            real_name = "strlen";
        else if (strcmp(saved->str, "__builtin_strcpy") == 0)
            real_name = "strcpy";
        else if (strcmp(saved->str, "__builtin_strncpy") == 0)
            real_name = "strncpy";
        else if (strcmp(saved->str, "__builtin_abort") == 0)
            real_name = "abort";
        else if (strcmp(saved->str, "__builtin_exit") == 0)
            real_name = "exit";
        else if (strcmp(saved->str, "__builtin_strchr") == 0)
            real_name = "strchr";
        if (real_name) {
            saved->str = str_dup(parse_arena, real_name,
                                 (int)strlen(real_name));
            saved->len = (int)strlen(real_name);
        }
    }

    if (find_enum_val(saved->str, &eval)) {
        var_n = new_num(eval);
    } else {
        int stmt_is_upvar;
        stmt_is_upvar = 0;
        sym = find_local_ex(saved->str, &stmt_is_upvar);
        if (sym == NULL) {
            sym = find_global(saved->str);
        }

        if (peek()->kind == TOK_LPAREN) {
            advance();
            var_n = new_node(ND_CALL);
            /* use asm_label for mangled nested functions */
            if (sym && sym->asm_label) {
                var_n->name = sym->asm_label;
            } else {
                var_n->name = str_dup(parse_arena, saved->str,
                    saved->len);
            }
            if (sym && sym->ty && sym->ty->kind == TY_PTR &&
                sym->ty->base && sym->ty->base->kind == TY_FUNC) {
                /* function pointer call */
                struct node *vr;
                var_n->ty = sym->ty->base->ret;
                vr = new_node(ND_VAR);
                vr->name = var_n->name;
                vr->ty = sym->ty;
                vr->offset = sym->offset;
                var_n->lhs = vr;
            } else if (sym && sym->ty && sym->ty->kind == TY_FUNC) {
                struct node *vr;
                var_n->ty = sym->ty->ret;
                vr = new_node(ND_VAR);
                vr->name = var_n->name;
                vr->ty = sym->ty;
                vr->offset = sym->offset;
                var_n->lhs = vr;
            } else {
                var_n->ty = ty_int;
            }
            {
                struct type *par_ty;
                struct type *fty;
                fty = NULL;
                if (sym && sym->ty &&
                    sym->ty->kind == TY_FUNC) {
                    fty = sym->ty;
                } else if (sym && sym->ty &&
                           sym->ty->kind == TY_PTR &&
                           sym->ty->base &&
                           sym->ty->base->kind == TY_FUNC) {
                    fty = sym->ty->base;
                }
                par_ty = fty ? fty->params : NULL;
                arg_head.next = NULL;
                ca = &arg_head;
                while (peek()->kind != TOK_RPAREN &&
                       peek()->kind != TOK_EOF) {
                    arg = parse_assign();
                    add_type(arg);
                    /* implicit cast for type mismatch */
                    if (par_ty != NULL && arg->ty != NULL
                        && arg->ty->kind != par_ty->kind) {
                        int afp, pfp;
                        afp = type_is_flonum(arg->ty);
                        pfp = type_is_flonum(par_ty);
                        if (afp != pfp ||
                            arg->ty->size != par_ty->size) {
                            struct node *cast;
                            cast = new_unary(ND_CAST, arg);
                            cast->ty = par_ty;
                            arg = cast;
                        }
                    }
                    ca->next = arg;
                    ca = arg;
                    ca->next = NULL;
                    if (par_ty != NULL)
                        par_ty = par_ty->next;
                    if (peek()->kind != TOK_COMMA) break;
                    advance();
                }
            }
            var_n->args = arg_head.next;
            expect(TOK_RPAREN, "')'");
        } else {
            if (sym == NULL) {
                sym = add_global(saved->str, ty_int);
            }
            var_n = new_node(ND_VAR);
            if (sym->asm_label) {
                var_n->name = sym->asm_label;
            } else {
                var_n->name = str_dup(parse_arena, saved->str,
                                      saved->len);
            }
            var_n->ty = sym->ty;
            var_n->offset = sym->offset;
            var_n->attr_flags = sym->attr_flags;
            var_n->is_upvar = stmt_is_upvar;
        }
    }

    /* handle postfix operations */
    for (;;) {
        if (peek()->kind == TOK_LBRACKET) {
            advance();
            idx = parse_expr();
            add_type(var_n);
            add_type(idx);
            var_n = new_unary(ND_DEREF, new_binary(ND_ADD, var_n, idx));
            add_type(var_n);
            expect(TOK_RBRACKET, "']'");
            continue;
        }
        if (peek()->kind == TOK_DOT) {
            advance();
            add_type(var_n);
            t = expect(TOK_IDENT, "member name");
            if (var_n->ty && (var_n->ty->kind == TY_STRUCT
                              || var_n->ty->kind == TY_UNION)) {
                mem = type_find_member(var_n->ty, t->str);
                if (mem) {
                    mn = new_node(ND_MEMBER);
                    mn->lhs = var_n;
                    mn->name = str_dup(parse_arena, t->str, t->len);
                    mn->ty = mem->ty;
                    mn->offset = mem->offset;
                    mn->bit_width = mem->bit_width;
                    mn->bit_offset = mem->bit_offset;
                    var_n = mn;
                }
            }
            continue;
        }
        if (peek()->kind == TOK_ARROW) {
            advance();
            add_type(var_n);
            t = expect(TOK_IDENT, "member name");
            var_n = new_unary(ND_DEREF, var_n);
            add_type(var_n);
            if (var_n->ty && (var_n->ty->kind == TY_STRUCT
                              || var_n->ty->kind == TY_UNION)) {
                mem = type_find_member(var_n->ty, t->str);
                if (mem) {
                    mn = new_node(ND_MEMBER);
                    mn->lhs = var_n;
                    mn->name = str_dup(parse_arena, t->str, t->len);
                    mn->ty = mem->ty;
                    mn->offset = mem->offset;
                    mn->bit_width = mem->bit_width;
                    mn->bit_offset = mem->bit_offset;
                    var_n = mn;
                }
            }
            continue;
        }
        if (peek()->kind == TOK_LPAREN) {
            /* function call through function pointer or member access */
            struct node call_ah;
            struct node *call_at;
            struct node *call_a;
            struct node *cn;

            advance();
            add_type(var_n);
            cn = new_node(ND_CALL);
            cn->lhs = var_n;
            if (var_n->ty && var_n->ty->kind == TY_PTR &&
                var_n->ty->base && var_n->ty->base->kind == TY_FUNC) {
                cn->ty = var_n->ty->base->ret;
            } else if (var_n->ty && var_n->ty->kind == TY_FUNC) {
                cn->ty = var_n->ty->ret;
            } else {
                cn->ty = ty_int;
            }
            if (var_n->name) {
                cn->name = var_n->name;
            }
            call_ah.next = NULL;
            call_at = &call_ah;
            while (peek()->kind != TOK_RPAREN &&
                   peek()->kind != TOK_EOF) {
                call_a = parse_assign();
                add_type(call_a);
                call_at->next = call_a;
                call_at = call_a;
                call_at->next = NULL;
                if (peek()->kind != TOK_COMMA) break;
                advance();
            }
            cn->args = call_ah.next;
            expect(TOK_RPAREN, "')'");
            var_n = cn;
            continue;
        }
        if (peek()->kind == TOK_INC) {
            advance();
            var_n = new_unary(ND_POST_INC, var_n);
            add_type(var_n);
            continue;
        }
        if (peek()->kind == TOK_DEC) {
            advance();
            var_n = new_unary(ND_POST_DEC, var_n);
            add_type(var_n);
            continue;
        }
        break;
    }

    /* handle assignment */
    if (peek()->kind == TOK_ASSIGN) {
        advance();
        rhs_node = parse_assign();
        var_n = new_binary(ND_ASSIGN, var_n, rhs_node);
        add_type(var_n);
        /* comma operator after assignment: a = b, c = d; */
        while (peek()->kind == TOK_COMMA) {
            advance();
            rhs_node = parse_assign();
            add_type(rhs_node);
            var_n = new_binary(ND_COMMA_EXPR, var_n, rhs_node);
            add_type(var_n);
        }
    } else if (peek()->kind >= TOK_PLUS_EQ && peek()->kind <= TOK_RSHIFT_EQ) {
        aop = peek()->kind;
        advance();
        rhs_node = parse_assign();
        switch (aop) {
        case TOK_PLUS_EQ:    ank = ND_ADD; break;
        case TOK_MINUS_EQ:   ank = ND_SUB; break;
        case TOK_STAR_EQ:    ank = ND_MUL; break;
        case TOK_SLASH_EQ:   ank = ND_DIV; break;
        case TOK_PERCENT_EQ: ank = ND_MOD; break;
        case TOK_AMP_EQ:     ank = ND_BITAND; break;
        case TOK_PIPE_EQ:    ank = ND_BITOR; break;
        case TOK_CARET_EQ:   ank = ND_BITXOR; break;
        case TOK_LSHIFT_EQ:  ank = ND_SHL; break;
        case TOK_RSHIFT_EQ:  ank = ND_SHR; break;
        default:             ank = ND_ADD; break;
        }
        var_n = new_binary(ND_ASSIGN, var_n, new_binary(ank, var_n, rhs_node));
        add_type(var_n);
        /* comma operator after compound assignment */
        while (peek()->kind == TOK_COMMA) {
            advance();
            rhs_node = parse_assign();
            add_type(rhs_node);
            var_n = new_binary(ND_COMMA_EXPR, var_n, rhs_node);
            add_type(var_n);
        }
    } else if (peek()->kind != TOK_SEMI) {
        /*
         * General binary/ternary expression: the identifier was the
         * start of a complex expression like "a > b ? a : b".
         * Continue parsing binary operators with the LHS we built.
         */
        add_type(var_n);
        for (;;) {
            if (peek()->kind == TOK_PLUS) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_ADD, var_n, rhs_node);
            } else if (peek()->kind == TOK_MINUS) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_SUB, var_n, rhs_node);
            } else if (peek()->kind == TOK_STAR) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_MUL, var_n, rhs_node);
            } else if (peek()->kind == TOK_SLASH) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_DIV, var_n, rhs_node);
            } else if (peek()->kind == TOK_PERCENT) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_MOD, var_n, rhs_node);
            } else if (peek()->kind == TOK_LT) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_LT, var_n, rhs_node);
            } else if (peek()->kind == TOK_GT) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_LT, rhs_node, var_n);
            } else if (peek()->kind == TOK_LE) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_LE, var_n, rhs_node);
            } else if (peek()->kind == TOK_GE) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_LE, rhs_node, var_n);
            } else if (peek()->kind == TOK_EQ) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_EQ, var_n, rhs_node);
            } else if (peek()->kind == TOK_NE) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_NE, var_n, rhs_node);
            } else if (peek()->kind == TOK_AND) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_LOGAND, var_n, rhs_node);
            } else if (peek()->kind == TOK_OR) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_LOGOR, var_n, rhs_node);
            } else if (peek()->kind == TOK_AMP) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_BITAND, var_n, rhs_node);
            } else if (peek()->kind == TOK_PIPE) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_BITOR, var_n, rhs_node);
            } else if (peek()->kind == TOK_CARET) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_BITXOR, var_n, rhs_node);
            } else if (peek()->kind == TOK_LSHIFT) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_SHL, var_n, rhs_node);
            } else if (peek()->kind == TOK_RSHIFT) {
                advance(); rhs_node = parse_assign();
                var_n = new_binary(ND_SHR, var_n, rhs_node);
            } else if (peek()->kind == TOK_QUESTION) {
                struct node *tn;
                advance();
                tn = new_node(ND_TERNARY);
                tn->cond = var_n;
                tn->then_ = parse_expr();
                expect(TOK_COLON, "':'");
                tn->els = parse_ternary();
                add_type(tn);
                var_n = tn;
            } else if (peek()->kind == TOK_COMMA) {
                advance();
                rhs_node = parse_assign();
                var_n = new_binary(ND_COMMA_EXPR, var_n, rhs_node);
            } else {
                break;
            }
            add_type(var_n);
        }
    }

    expect(TOK_SEMI, "';'");
    return var_n;
}

/* ---- statement parsing ---- */

static struct node *parse_stmt(void)
{
    struct tok *t;
    static struct tok saved;
    struct node *n;
    struct node *case_expr;
    struct node *saved_switch_node;
    int saved_brk;
    int saved_cont;

    t = peek();

    if (t->kind == TOK_LBRACE) {
        return parse_compound_stmt();
    }

    if (t->kind == TOK_RETURN) {
        advance();
        n = new_node(ND_RETURN);
        if (peek()->kind != TOK_SEMI) {
            n->lhs = parse_expr();
            add_type(n->lhs);
            /* insert cast to function return type if needed */
            if (current_ret_type && n->lhs->ty &&
                current_ret_type != n->lhs->ty &&
                !type_is_compatible(current_ret_type, n->lhs->ty)) {
                struct node *cast;
                cast = new_unary(ND_CAST, n->lhs);
                cast->ty = current_ret_type;
                n->lhs = cast;
            }
        }
        expect(TOK_SEMI, "';'");
        return n;
    }

    if (t->kind == TOK_IF) {
        advance();
        n = new_node(ND_IF);
        expect(TOK_LPAREN, "'('");
        n->cond = parse_expr();
        add_type(n->cond);
        expect(TOK_RPAREN, "')'");
        n->then_ = parse_stmt();
        if (peek()->kind == TOK_ELSE) {
            advance();
            n->els = parse_stmt();
        }
        return n;
    }

    if (t->kind == TOK_WHILE) {
        saved_brk = brk_label;
        saved_cont = cont_label;

        advance();
        n = new_node(ND_WHILE);
        n->label_id = new_label();
        brk_label = new_label();
        cont_label = n->label_id;

        expect(TOK_LPAREN, "'('");
        n->cond = parse_expr();
        add_type(n->cond);
        expect(TOK_RPAREN, "')'");
        n->then_ = parse_stmt();

        brk_label = saved_brk;
        cont_label = saved_cont;
        return n;
    }

    if (t->kind == TOK_FOR) {
        int for_has_scope;

        saved_brk = brk_label;
        saved_cont = cont_label;
        for_has_scope = 0;

        advance();
        n = new_node(ND_FOR);
        n->label_id = new_label();
        brk_label = new_label();
        cont_label = new_label();

        expect(TOK_LPAREN, "'('");

        /* C99: for-loop variable declaration */
        if (cc_has_feat(FEAT_FOR_DECL) && is_type_token(peek())) {
            enter_scope();
            for_has_scope = 1;
            n->init = parse_declaration();
            /* parse_declaration consumed the ';' */
        } else {
            if (peek()->kind != TOK_SEMI) {
                n->init = parse_expr();
                add_type(n->init);
            }
            expect(TOK_SEMI, "';'");
        }

        if (peek()->kind != TOK_SEMI) {
            n->cond = parse_expr();
            add_type(n->cond);
        }
        expect(TOK_SEMI, "';'");
        if (peek()->kind != TOK_RPAREN) {
            n->inc = parse_expr();
            add_type(n->inc);
        }
        expect(TOK_RPAREN, "')'");
        n->then_ = parse_stmt();

        /* leave scope if we entered one for for-decl */
        if (for_has_scope) {
            leave_scope();
        }

        brk_label = saved_brk;
        cont_label = saved_cont;
        return n;
    }

    if (t->kind == TOK_DO) {
        saved_brk = brk_label;
        saved_cont = cont_label;

        advance();
        n = new_node(ND_DO);
        n->label_id = new_label();
        brk_label = new_label();
        cont_label = n->label_id;

        n->then_ = parse_stmt();
        expect(TOK_WHILE, "'while'");
        expect(TOK_LPAREN, "'('");
        n->cond = parse_expr();
        add_type(n->cond);
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");

        brk_label = saved_brk;
        cont_label = saved_cont;
        return n;
    }

    if (t->kind == TOK_SWITCH) {
        saved_switch_node = cur_switch;
        saved_brk = brk_label;

        advance();
        n = new_node(ND_SWITCH);
        n->label_id = new_label();
        brk_label = new_label();
        cur_switch = n;

        expect(TOK_LPAREN, "'('");
        n->cond = parse_expr();
        add_type(n->cond);
        expect(TOK_RPAREN, "')'");
        n->body = parse_stmt();

        cur_switch = saved_switch_node;
        brk_label = saved_brk;
        return n;
    }

    if (t->kind == TOK_CASE) {
        advance();
        n = new_node(ND_CASE);
        n->label_id = new_label();

        case_expr = parse_ternary();
        add_type(case_expr);
        if (case_expr->kind == ND_NUM) {
            n->val = case_expr->val;
        }

        /* GCC case range extension: case 1 ... 5: */
        if (peek()->kind == TOK_ELLIPSIS) {
            struct node *hi_expr;
            advance(); /* consume '...' */
            hi_expr = parse_ternary();
            add_type(hi_expr);
            if (hi_expr->kind == ND_NUM) {
                n->case_end = hi_expr->val;
            }
            n->is_case_range = 1;
        }

        expect(TOK_COLON, "':'");
        if (peek()->kind == TOK_RBRACE) {
            n->body = new_node(ND_BLOCK);
        } else {
            n->body = parse_stmt();
        }
        return n;
    }

    if (t->kind == TOK_DEFAULT) {
        advance();
        expect(TOK_COLON, "':'");
        n = new_node(ND_CASE);
        n->label_id = new_label();
        n->val = 0;
        n->is_default = 1;
        if (peek()->kind == TOK_RBRACE) {
            n->body = new_node(ND_BLOCK);
        } else {
            n->body = parse_stmt();
        }
        return n;
    }

    if (t->kind == TOK_BREAK) {
        advance();
        expect(TOK_SEMI, "';'");
        n = new_node(ND_BREAK);
        n->label_id = brk_label;
        return n;
    }

    if (t->kind == TOK_CONTINUE) {
        advance();
        expect(TOK_SEMI, "';'");
        n = new_node(ND_CONTINUE);
        n->label_id = cont_label;
        return n;
    }

    if (t->kind == TOK_GOTO) {
        advance();
        /* computed goto: goto *expr */
        if (peek()->kind == TOK_STAR) {
            advance();
            n = new_node(ND_GOTO_INDIRECT);
            n->lhs = parse_expr();
            add_type(n->lhs);
            expect(TOK_SEMI, "';'");
            return n;
        }
        t = expect(TOK_IDENT, "label name");
        expect(TOK_SEMI, "';'");
        n = new_node(ND_GOTO);
        n->name = str_dup(parse_arena, t->str, t->len);
        /* If inside a nested function and this label was declared
         * via __label__ in the enclosing function, use the
         * enclosing function name for label resolution. */
        if (is_enclosing_label(t->str)) {
            n->section_name = str_dup(parse_arena,
                enclosing_func_name,
                (int)strlen(enclosing_func_name));
        }
        return n;
    }

    /* declaration (check before ident branch so type-name idents
     * like __builtin_va_list or typedefs go to parse_declaration) */
    if (is_type_token(t) && t->kind != TOK_IDENT) {
        return parse_declaration();
    }

    /* label or expression statement starting with identifier */
    if (t->kind == TOK_IDENT) {
        /* GNU __extension__ before a declaration or expression */
        if (t->str && attr_is_extension_keyword(t)) {
            advance();
            return parse_stmt();
        }

        /* GNU __attribute__((...)) as standalone stmt or before decl */
        if (t->str && attr_is_attribute_keyword(t)) {
            skip_attribute();
            /* after consuming __attribute__, re-parse as statement */
            return parse_stmt();
        }

        /* GNU inline asm statement */
        if (t->str && asm_is_asm_keyword(t)) {
            struct node *asm_node;
            struct asm_stmt *as;
            as = asm_alloc_stmt(parse_arena);
            advance(); /* consume asm keyword */
            /* set up constant expression evaluator for "i" constraints */
            asm_set_const_eval(asm_const_expr_eval);
            /* asm_parse expects cur_tok to be past the keyword */
            asm_parse(as, &cur_tok);
            /* resolve local variable offsets */
            asm_resolve_operands(as, asm_var_lookup);
            asm_node = new_node(ND_GCC_ASM);
            asm_node->asm_data = as;
            return asm_node;
        }

        /* if it's a type name (typedef or __builtin_va_list), check
         * if it's actually a label (ident:) before parsing as decl */
        if (is_type_token(t)) {
            /* peek ahead: if next token is ':', it's a label */
            saved = *t;
            advance();
            if (peek()->kind == TOK_COLON) {
                /* fall through to label handling below */
                goto handle_label;
            }
            unadvance(&saved);
            return parse_declaration();
        }
        saved = *t;
        advance();
        if (peek()->kind == TOK_COLON) {
            handle_label:
            advance();
            n = new_node(ND_LABEL);
            n->name = str_dup(parse_arena, saved.str, saved.len);
            n->label_id = new_label();
            /* gcc extension: label at end of compound statement */
            if (peek()->kind == TOK_RBRACE) {
                n->body = new_node(ND_BLOCK);
            } else {
                n->body = parse_stmt();
            }
            return n;
        }
        return parse_expr_stmt_from_ident(&saved);
    }

    /* empty statement */
    if (t->kind == TOK_SEMI) {
        advance();
        return new_node(ND_BLOCK);
    }

    /* expression statement */
    n = parse_expr();
    add_type(n);
    expect(TOK_SEMI, "';'");
    return n;
}

/* compound statement { ... } */
static struct node *parse_compound_stmt(void)
{
    struct node *n;
    struct node head;
    struct node *cur_stmt;
    struct node *s;

    expect(TOK_LBRACE, "'{'");
    enter_scope();

    n = new_node(ND_BLOCK);
    head.next = NULL;
    cur_stmt = &head;

    /* GNU __label__ declarations at top of block */
    while (peek()->kind == TOK_IDENT &&
           peek()->str != NULL &&
           strcmp(peek()->str, "__label__") == 0) {
        advance(); /* consume __label__ */
        /* parse comma-separated list of label names */
        for (;;) {
            if (peek()->kind == TOK_IDENT) {
                /* record this label for nested function resolution */
                if (n_enclosing_labels < MAX_ENCLOSING_LABELS) {
                    enclosing_labels[n_enclosing_labels++] =
                        str_dup(parse_arena,
                            peek()->str, peek()->len);
                }
                advance(); /* consume label name */
            }
            if (peek()->kind != TOK_COMMA) {
                break;
            }
            advance(); /* consume ',' */
        }
        expect(TOK_SEMI, "';'");
    }

    while (peek()->kind != TOK_RBRACE && peek()->kind != TOK_EOF) {
        if (is_type_token(peek()) && peek()->kind == TOK_IDENT) {
            /* typedef names go through parse_stmt which handles
             * the label-vs-declaration ambiguity (ident:) */
            s = parse_stmt();
        } else if (is_type_token(peek())) {
            s = parse_declaration();
        } else {
            s = parse_stmt();
        }

        if (s != NULL) {
            cur_stmt->next = s;
            while (cur_stmt->next != NULL) {
                cur_stmt = cur_stmt->next;
            }
        }
    }

    n->body = head.next;
    expect(TOK_RBRACE, "'}'");
    leave_scope();
    return n;
}

/* ---- top-level parsing ---- */

/*
 * find_member_by_name - look up a struct/union member by name.
 */
static struct member *find_member_by_name(struct type *ty,
                                          const char *name)
{
    if (ty == NULL) return NULL;
    return type_find_member(ty, name);
}

static int init_list_has_index_designator(struct node *ilist)
{
    struct node *cur;

    if (ilist == NULL || ilist->kind != ND_INIT_LIST) {
        return 0;
    }

    for (cur = ilist->body; cur != NULL; cur = cur->next) {
        if (cur->kind == ND_DESIG_INIT && cur->name == NULL) {
            return 1;
        }
    }

    return 0;
}

static struct node *advance_init_one(struct node *cur, struct type *ty)
{
    struct member *m;
    struct node *next;
    int i;

    if (cur == NULL || ty == NULL) {
        return cur;
    }

    if (cur->kind == ND_INIT_LIST || cur->kind == ND_COMPOUND_LIT) {
        return cur->next;
    }

    if (ty->kind == TY_ARRAY && ty->base != NULL) {
        if (cur->kind == ND_STR) {
            return cur->next;
        }
        if (ty->array_len <= 0) {
            return cur->next;
        }
        next = cur;
        for (i = 0; i < ty->array_len && next != NULL; i++) {
            next = advance_init_one(next, ty->base);
        }
        return next;
    }

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        next = cur;
        for (m = ty->members; m != NULL && next != NULL; m = m->next) {
            next = advance_init_one(next, m->ty);
            if (ty->kind == TY_UNION) {
                break;
            }
        }
        return next;
    }

    return cur->next;
}

static void set_inferred_array_len(struct type *ty, int len)
{
    if (ty == NULL || ty->kind != TY_ARRAY || ty->base == NULL) {
        return;
    }

    if (len < 0) {
        len = 0;
    }
    ty->array_len = len;
    ty->size = ty->base->size * len;
    ty->align = ty->base->align;
    ty->origin = ty;
}

static int infer_unsized_array_len(struct type *ty, struct node *ilist)
{
    struct node *cur;
    struct node *next;
    int len;
    int cur_idx;
    int max_idx;

    if (ty == NULL || ilist == NULL || ilist->kind != ND_INIT_LIST ||
        ty->kind != TY_ARRAY || ty->base == NULL) {
        return 0;
    }

    if (init_list_has_index_designator(ilist)) {
        cur_idx = 0;
        max_idx = -1;
        for (cur = ilist->body; cur != NULL; cur = cur->next) {
            if (cur->kind == ND_DESIG_INIT && cur->name == NULL) {
                cur_idx = (int)cur->val;
            }
            if (cur_idx > max_idx) {
                max_idx = cur_idx;
            }
            cur_idx++;
        }
        return max_idx + 1;
    }

    len = 0;
    cur = ilist->body;
    while (cur != NULL) {
        next = advance_init_one(cur, ty->base);
        if (next == cur) {
            next = cur->next;
        }
        cur = next;
        len++;
    }
    return len;
}

/*
 * parse_global_init - parse a global variable initializer expression.
 * Handles brace-enclosed lists (including nested braces for struct arrays),
 * designated initializers (.field = val, [idx] = val), and single
 * expressions. Returns an ND_INIT_LIST or expression node.
 */
static struct node *parse_global_init(struct type *ty)
{
    struct node *ilist;
    struct node *ie;

    if (peek()->kind == TOK_LBRACE) {
        struct node head;
        struct node *tail;
        struct member *pos_mbr;

        ilist = new_node(ND_INIT_LIST);
        ilist->ty = ty;
        head.next = NULL;
        tail = &head;
        pos_mbr = NULL;
        if (ty != NULL && (ty->kind == TY_STRUCT ||
                           ty->kind == TY_UNION)) {
            pos_mbr = ty->members;
        }
        advance(); /* consume '{' */
        while (peek()->kind != TOK_RBRACE &&
               peek()->kind != TOK_EOF) {

            /* designated initializer: .field = value
             * or chained: .field1.field2 = value */
            if (peek()->kind == TOK_DOT &&
                cc_has_feat(FEAT_DESIG_INIT)) {
                struct member *dm;
                struct type *cur_ty;
                char *fname;
                int flen;
                int total_off;

                advance(); /* consume '.' */
                fname = (char *)peek()->str;
                flen = peek()->len;
                advance(); /* consume field name */

                ie = new_node(ND_DESIG_INIT);
                ie->name = str_dup(parse_arena, fname, flen);

                dm = NULL;
                total_off = 0;
                cur_ty = ty;
                if (cur_ty != NULL) {
                    dm = find_member_by_name(cur_ty, ie->name);
                }
                if (dm != NULL) {
                    total_off = dm->offset;
                    cur_ty = dm->ty;
                }

                /* handle chained designators: .a.b.c = val */
                while (peek()->kind == TOK_DOT) {
                    advance(); /* consume '.' */
                    fname = (char *)peek()->str;
                    flen = peek()->len;
                    advance(); /* consume field name */

                    dm = NULL;
                    if (cur_ty != NULL) {
                        dm = find_member_by_name(cur_ty,
                            str_dup(parse_arena, fname, flen));
                    }
                    if (dm != NULL) {
                        total_off += dm->offset;
                        cur_ty = dm->ty;
                    }
                }

                ie->offset = total_off;
                ie->ty = cur_ty;

                expect(TOK_ASSIGN, "'='");

                if (peek()->kind == TOK_LBRACE) {
                    ie->lhs = parse_global_init(cur_ty);
                } else {
                    ie->lhs = parse_assign();
                    add_type(ie->lhs);
                }
                ie->lhs = constant_fold(ie->lhs);

                if (dm != NULL) {
                    pos_mbr = dm->next;
                }

                tail->next = ie;
                tail = ie;
                ie->next = NULL;
                if (peek()->kind == TOK_COMMA) {
                    advance();
                }
                continue;
            }

            /* designated initializer: [index] = value
             * or GCC range: [low ... high] = value */
            if (peek()->kind == TOK_LBRACKET &&
                cc_has_feat(FEAT_DESIG_INIT)) {
                struct node *idx_expr;
                long idx_val;
                long range_hi;
                int is_range;

                advance(); /* consume '[' */
                idx_expr = parse_assign();
                add_type(idx_expr);
                idx_expr = constant_fold(idx_expr);
                idx_val = 0;
                if (idx_expr->kind == ND_NUM) {
                    idx_val = idx_expr->val;
                }

                /* GCC range designator: [low ... high] */
                is_range = 0;
                range_hi = idx_val;
                if (peek()->kind == TOK_ELLIPSIS) {
                    struct node *hi_expr;
                    advance(); /* consume '...' */
                    hi_expr = parse_assign();
                    add_type(hi_expr);
                    hi_expr = constant_fold(hi_expr);
                    if (hi_expr->kind == ND_NUM) {
                        range_hi = hi_expr->val;
                    }
                    is_range = 1;
                }

                expect(TOK_RBRACKET, "']'");
                expect(TOK_ASSIGN, "'='");

                if (is_range) {
                    /* expand [low ... high] = val into multiple
                     * ND_DESIG_INIT nodes */
                    struct node *val_expr;
                    long ri;
                    if (peek()->kind == TOK_LBRACE) {
                        val_expr = parse_global_init(
                            ty ? ty->base : NULL);
                    } else {
                        val_expr = parse_assign();
                        add_type(val_expr);
                    }
                    val_expr = constant_fold(val_expr);
                    for (ri = idx_val; ri <= range_hi; ri++) {
                        ie = new_node(ND_DESIG_INIT);
                        ie->val = ri;
                        ie->name = NULL;
                        if (ty != NULL && ty->base != NULL) {
                            ie->offset =
                                (int)(ri * ty->base->size);
                            ie->ty = ty->base;
                        }
                        ie->lhs = val_expr;
                        ie->next = NULL;
                        tail->next = ie;
                        tail = ie;
                    }
                } else {
                    ie = new_node(ND_DESIG_INIT);
                    ie->val = idx_val;
                    ie->name = NULL;
                    if (ty != NULL && ty->base != NULL) {
                        ie->offset =
                            (int)(idx_val * ty->base->size);
                        ie->ty = ty->base;
                    }

                    if (peek()->kind == TOK_LBRACE) {
                        ie->lhs = parse_global_init(
                            ty ? ty->base : NULL);
                    } else {
                        ie->lhs = parse_assign();
                        add_type(ie->lhs);
                    }
                    ie->lhs = constant_fold(ie->lhs);

                    tail->next = ie;
                    tail = ie;
                    ie->next = NULL;
                }
                if (peek()->kind == TOK_COMMA) {
                    advance();
                }
                continue;
            }

            /* GNU designated initializer: field: value */
            if (peek()->kind == TOK_IDENT &&
                cc_has_feat(FEAT_DESIG_INIT) &&
                ty != NULL &&
                (ty->kind == TY_STRUCT || ty->kind == TY_UNION)) {
                struct tok *id_tok;
                id_tok = advance();
                if (peek()->kind == TOK_COLON) {
                    struct member *dm;
                    advance();

                    ie = new_node(ND_DESIG_INIT);
                    ie->name = str_dup(parse_arena,
                                       id_tok->str, id_tok->len);
                    dm = find_member_by_name(ty, ie->name);
                    if (dm != NULL) {
                        ie->offset = dm->offset;
                        ie->ty = dm->ty;
                    }

                    if (peek()->kind == TOK_LBRACE) {
                        ie->lhs = parse_global_init(
                            dm ? dm->ty : NULL);
                    } else {
                        ie->lhs = parse_assign();
                    }
                    add_type(ie->lhs);
                    ie->lhs = constant_fold(ie->lhs);

                    tail->next = ie;
                    tail = ie;
                    ie->next = NULL;
                    if (peek()->kind == TOK_COMMA) {
                        advance();
                    }
                    continue;
                }
                unadvance(id_tok);
            }

            if (peek()->kind == TOK_LBRACE) {
                /* nested brace initializer */
                struct type *elem_ty;
                elem_ty = NULL;
                if (ty != NULL && ty->kind == TY_ARRAY &&
                    ty->base != NULL) {
                    elem_ty = ty->base;
                } else if (ty != NULL &&
                           (ty->kind == TY_STRUCT ||
                            ty->kind == TY_UNION) &&
                           pos_mbr != NULL) {
                    elem_ty = pos_mbr->ty;
                }
                ie = parse_global_init(elem_ty);
            } else {
                ie = parse_assign();
                add_type(ie);
            }
            tail->next = ie;
            tail = ie;
            ie->next = NULL;
            if (pos_mbr != NULL) {
                pos_mbr = pos_mbr->next;
            }
            if (peek()->kind == TOK_COMMA) {
                advance();
            }
        }
        if (peek()->kind == TOK_RBRACE) {
            advance();
        }
        /* store children in body, not next (next is for sibling links) */
        ilist->body = head.next;
        return ilist;
    } else {
        ie = parse_assign();
        add_type(ie);
        ie = constant_fold(ie);
        return ie;
    }
}

/* parse function definition or global declaration */
static struct node *parse_toplevel(void)
{
    struct type *base_ty;
    struct type *ty;
    struct type *p;
    char *name;
    int is_td;
    int is_ext;
    int is_stat;
    struct symbol *sym;
    struct node *fn;
    char *decl_section_name;
    struct attr_info decl_attrs;

    /* skip leading __extension__ at top level */
    while (peek()->kind == TOK_IDENT && peek()->str &&
           attr_is_extension_keyword(peek())) {
        advance();
    }

    /* skip C23 [[ ... ]] attributes at top level */
    while (peek()->kind == TOK_LBRACKET) {
        if (!skip_c23_attr()) break;
    }

    /* C11: _Static_assert at top level */
    if ((peek()->kind == TOK_STATIC_ASSERT &&
         cc_has_feat(FEAT_STATIC_ASSERT)) ||
        (peek()->kind == TOK_STATIC_ASSERT_KW &&
         cc_has_feat2(FEAT2_STATIC_ASSERT_NS))) {
        struct tok *sa_tok;
        struct node *sa_expr;
        sa_tok = peek();
        advance();
        expect(TOK_LPAREN, "'('");
        sa_expr = parse_assign();
        add_type(sa_expr);
        if (peek()->kind == TOK_COMMA) {
            advance();
            /* parse message expression (handles string concat) */
            parse_assign();
        }
        expect(TOK_RPAREN, "')'");
        expect(TOK_SEMI, "';'");
        if (sa_expr->kind == ND_NUM && sa_expr->val == 0) {
            diag_warn(sa_tok->file, sa_tok->line, sa_tok->col,
                      "static assertion failed (warning)");
        }
        return NULL;
    }

    /* skip standalone asm("...") at top level (e.g. asm(".symver ...")) */
    if (peek()->kind == TOK_IDENT && peek()->str &&
        asm_is_asm_keyword(peek())) {
        int depth;
        advance(); /* consume asm */
        while (is_asm_qualifier(peek())) {
            advance();
        }
        if (peek()->kind == TOK_LPAREN) {
            advance();
            depth = 1;
            while (depth > 0 && peek()->kind != TOK_EOF) {
                if (peek()->kind == TOK_LPAREN) depth++;
                else if (peek()->kind == TOK_RPAREN) depth--;
                advance();
            }
        }
        if (peek()->kind == TOK_SEMI) advance();
        return NULL;
    }

    /* capture leading __attribute__((...)) before type specifiers */
    attr_info_init(&decl_attrs);
    decl_section_name = NULL;
    if (peek()->kind == TOK_IDENT && peek()->str &&
        attr_is_attribute_keyword(peek())) {
        attr_try_parse(&decl_attrs, &cur_tok);
        decl_section_name = decl_attrs.section_name;
    }

    declspec_attrs = &decl_attrs;
    base_ty = parse_declspec(&is_td, &is_ext, &is_stat);
    declspec_attrs = NULL;
    if (decl_attrs.section_name)
        decl_section_name = decl_attrs.section_name;

    if (peek()->kind == TOK_SEMI) {
        advance();
        return NULL;
    }

    name = NULL;
    ty = parse_declarator(base_ty, &name);

    if (is_td) {
        /* apply __attribute__ parsed by parse_declarator to the type
         * (e.g. aligned, packed, vector_size on typedefs) */
        skip_all_attrs_and_ext();
        if (declarator_attrs.aligned > 0 || declarator_attrs.flags ||
            declarator_attrs.vector_size > 0) {
            attr_apply_to_type(ty, &declarator_attrs);
        }
        if (name) {
            add_typedef(name, ty);
        }
        while (peek()->kind == TOK_COMMA) {
            advance();
            name = NULL;
            ty = parse_declarator(base_ty, &name);
            skip_all_attrs_and_ext();
            if (declarator_attrs.aligned > 0 || declarator_attrs.flags ||
                declarator_attrs.vector_size > 0) {
                attr_apply_to_type(ty, &declarator_attrs);
            }
            if (name) {
                add_typedef(name, ty);
            }
        }
        expect(TOK_SEMI, "';'");
        return NULL;
    }

    /* merge attrs parsed by parse_declarator */
    decl_attrs.flags |= declarator_attrs.flags;
    if (declarator_attrs.section_name)
        decl_section_name = declarator_attrs.section_name;
    if (declarator_attrs.alias_name)
        decl_attrs.alias_name = declarator_attrs.alias_name;
    /* parse any remaining __attribute__ between declarator and function body */
    {
        attr_try_parse(&decl_attrs, &cur_tok);
        if (decl_attrs.section_name)
            decl_section_name = decl_attrs.section_name;
    }
    /* also skip __extension__, asm(...), and raw kernel annotations */
    while (peek()->kind == TOK_IDENT && peek()->str) {
        if (attr_is_extension_keyword(peek())) {
            advance();
            continue;
        }
        if (is_kernel_annot(peek())) {
            skip_kernel_annot();
            continue;
        }
        if (asm_is_asm_keyword(peek())) {
            int d;
            advance();
            if (peek()->kind == TOK_LPAREN) {
                advance();
                d = 1;
                while (d > 0 && peek()->kind != TOK_EOF) {
                    if (peek()->kind == TOK_LPAREN) d++;
                    else if (peek()->kind == TOK_RPAREN) d--;
                    advance();
                }
            }
            continue;
        }
        if (attr_is_attribute_keyword(peek())) {
            attr_try_parse(&decl_attrs, &cur_tok);
            if (decl_attrs.section_name)
                decl_section_name = decl_attrs.section_name;
            continue;
        }
        break;
    }

    /* K&R old-style function definition:
     *   type func(a, b, c) int a; char *b; long c; { body }
     * After parsing func(a, b, c), params have implicit-int types.
     * Parse the parameter type declarations before the opening brace. */
    if (ty->kind == TY_FUNC && peek()->kind != TOK_LBRACE &&
        peek()->kind != TOK_SEMI && peek()->kind != TOK_COMMA &&
        peek()->kind != TOK_EOF &&
        (is_type_token(peek()) || peek()->kind == TOK_IDENT)) {
        struct type *kp;
        int found_kr;

        found_kr = 0;
        /* Check if any param has a matching name — confirms K&R style */
        for (kp = ty->params; kp != NULL; kp = kp->next) {
            if (kp->name != NULL) {
                found_kr = 1;
                break;
            }
        }
        if (found_kr) {
            /* Parse K&R parameter declarations */
            while (peek()->kind != TOK_LBRACE &&
                   peek()->kind != TOK_EOF) {
                struct type *kr_base;
                int kr_td, kr_ext, kr_stat;
                kr_base = parse_declspec(&kr_td, &kr_ext, &kr_stat);
                /* parse each declarator in the declaration */
                while (peek()->kind != TOK_SEMI &&
                       peek()->kind != TOK_EOF) {
                    char *kr_name;
                    struct type *kr_ty;
                    kr_name = NULL;
                    kr_ty = parse_declarator(kr_base, &kr_name);
                    /* find matching param and update type */
                    if (kr_name != NULL) {
                        for (kp = ty->params; kp != NULL;
                             kp = kp->next) {
                            if (kp->name != NULL &&
                                strcmp(kp->name, kr_name) == 0) {
                                kp->kind = kr_ty->kind;
                                kp->size = kr_ty->size;
                                kp->align = kr_ty->align;
                                kp->base = kr_ty->base;
                                kp->is_unsigned = kr_ty->is_unsigned;
                                kp->members = kr_ty->members;
                                break;
                            }
                        }
                    }
                    if (peek()->kind == TOK_COMMA) {
                        advance();
                    } else {
                        break;
                    }
                }
                if (peek()->kind == TOK_SEMI) {
                    advance();
                } else {
                    break;
                }
                /* skip __attribute__ between K&R decls */
                while (peek()->kind == TOK_IDENT && peek()->str &&
                       attr_is_attribute_keyword(peek())) {
                    char *more;
                    more = attr_parse_section_name(&cur_tok);
                    if (more) decl_section_name = more;
                }
            }
        }
    }

    /* function definition */
    if (ty->kind == TY_FUNC && peek()->kind == TOK_LBRACE) {
        struct node arg_head;
        struct node *arg_tail;
        int named_gp;
        int named_fp;
        int named_stack_bytes;

        fn = new_node(ND_FUNCDEF);
        fn->name = name;
        fn->ty = ty;
        fn->is_static = is_stat;
        fn->section_name = decl_section_name;
        fn->attr_flags = decl_attrs.flags;

        sym = add_global(name, ty);
        sym->is_defined = 1;

        /* enter function scope and add parameters as locals */
        current_ret_type = ty->ret;
        current_func_name = name;
        enclosing_func_name = NULL;
        n_enclosing_labels = 0;
        local_offset = 0;
        enter_scope();

        arg_head.next = NULL;
        arg_tail = &arg_head;
        named_gp = 0;
        named_fp = 0;

        p = ty->params;
        while (p != NULL) {
            if (p->name) {
                struct symbol *psym;
                struct node *pn;

                psym = add_local(p->name, p);

                /* build arg node for codegen parameter spilling */
                pn = new_node(ND_VAR);
                pn->name = psym->name;
                pn->ty = psym->ty;
                pn->offset = psym->offset;
                pn->next = NULL;
                arg_tail->next = pn;
                arg_tail = pn;
            }
            /* count named GP and FP register args.
             * Structs/unions <= 16 bytes consume ceil(size/8) GP regs.
             * Structs > 16 bytes are passed by reference (1 GP reg). */
            if (p->kind == TY_FLOAT || p->kind == TY_DOUBLE) {
                named_fp++;
            } else if ((p->kind == TY_STRUCT || p->kind == TY_UNION) &&
                       p->size <= 16) {
                named_gp += (p->size + 7) / 8;
            } else {
                named_gp++;
            }
            p = p->next;
        }
        fn->args = arg_head.next;

        /*
         * For variadic functions, reserve space for the GP register
         * save area (x0-x7 = 64 bytes).  The codegen prologue will
         * spill all 8 GPRs there.  Record the save area offset and
         * the number of named GP args so va_start can initialise
         * gr_offs correctly.
         *
         * Also compute how many named params are passed on the stack
         * so va_start can set __stack past them.
         */
        if (ty->is_variadic) {
            /* GP save area: x0-x7 = 64 bytes */
            local_offset += 64;
            local_offset = (local_offset + 15) & ~15;
            fn->va_save_offset = local_offset;
            fn->va_named_gp = named_gp;
            /* FP save area: d0-d7 = 128 bytes (16 per q-reg) */
            local_offset += 128;
            local_offset = (local_offset + 15) & ~15;
            fn->va_fp_save_offset = local_offset;
            fn->va_named_fp = named_fp;
            /* each stack-passed named arg occupies 8 bytes */
            named_stack_bytes = 0;
            if (named_gp > 8) {
                named_stack_bytes += (named_gp - 8) * 8;
            }
            if (named_fp > 8) {
                named_stack_bytes += (named_fp - 8) * 8;
            }
            fn->va_stack_start = 16 + named_stack_bytes;
        }

        fn->body = parse_compound_stmt();

        /* record total local variable space for frame size */
        fn->offset = local_offset;

        leave_scope();
        return fn;
    }

    /* global variable declaration - return ND_VAR nodes for codegen */
    {
        struct node ghead;
        struct node *gtail;
        struct node *gn;

        ghead.next = NULL;
        gtail = &ghead;

        /* extern function declarations: register symbol and store attrs */
        if (name != NULL && ty->kind == TY_FUNC) {
            sym = add_global(name, ty);
            sym->attr_flags = decl_attrs.flags;
            goto skip_global_init;
        }

        if (name != NULL && ty->kind != TY_FUNC) {
            sym = add_global(name, ty);
            sym->attr_flags = decl_attrs.flags;
            if (decl_attrs.alias_name) {
                sym->alias_target = decl_attrs.alias_name;
            }

            /* extern declarations don't emit storage */
            if (is_ext && peek()->kind != TOK_ASSIGN) {
                goto skip_global_init;
            }

            gn = new_node(ND_VAR);
            gn->name = name;
            gn->ty = ty;
            gn->offset = 0; /* marks it as global */
            gn->val = 0;
            gn->next = NULL;
            gn->is_static = is_stat;
            gn->section_name = decl_section_name;
            gn->attr_flags = decl_attrs.flags;

            /* handle initializer */
            if (peek()->kind == TOK_ASSIGN) {
                struct node *gi;
                advance();
                gi = parse_global_init(ty);
                /* array = string literal: size the array */
                if (gi != NULL && gi->kind == ND_STR &&
                    string_literal_matches_type(ty, gi)) {
                    int slen;
                    slen = gi->ty->array_len;
                    if (ty->array_len == 0 ||
                        ty->size == 0) {
                        set_inferred_array_len(ty, slen);
                    }
                    gn->ty = ty;
                    sym->ty = ty;
                }
                /* unsized array (int a[] = {1,2,3}):
                 * count init elements to size the type */
                if (gi != NULL && gi->kind == ND_INIT_LIST &&
                    ty->kind == TY_ARRAY && ty->base &&
                    ty->array_len == 0) {
                    int cnt;

                    cnt = infer_unsized_array_len(ty, gi);
                    set_inferred_array_len(ty, cnt);
                    gn->ty = ty;
                    sym->ty = ty;
                }
                if (gi != NULL && gi->kind == ND_NUM) {
                    gn->val = gi->val;
                } else if (gi != NULL) {
                    gn->init = gi;
                }
            }

            gtail->next = gn;
            gtail = gn;
        } else if (name != NULL) {
            /* function declaration (prototype) */
            sym = add_global(name, ty);
            if (peek()->kind == TOK_ASSIGN) {
                advance();
                if (peek()->kind == TOK_LBRACE) {
                    skip_braces();
                } else {
                    parse_assign();
                }
            }
        }

        skip_global_init:
        /* handle multiple declarations */
        while (peek()->kind == TOK_COMMA) {
            advance();
            name = NULL;
            ty = parse_declarator(base_ty, &name);
            if (name) {
                sym = add_global(name, ty);
                if (ty->kind != TY_FUNC) {
                    gn = new_node(ND_VAR);
                    gn->name = name;
                    gn->ty = ty;
                    gn->offset = 0;
                    gn->val = 0;
                    gn->next = NULL;
                    gn->is_static = is_stat;

                    if (peek()->kind == TOK_ASSIGN) {
                        struct node *gi;
                        advance();
                        gi = parse_global_init(ty);
                        if (gi != NULL && gi->kind == ND_NUM) {
                            gn->val = gi->val;
                        } else if (gi != NULL) {
                            gn->init = gi;
                        }
                    }

                    gtail->next = gn;
                    gtail = gn;
                } else if (peek()->kind == TOK_ASSIGN) {
                    advance();
                    if (peek()->kind == TOK_LBRACE) {
                        skip_braces();
                    } else {
                        parse_assign();
                    }
                }
            }
        }

        expect(TOK_SEMI, "';'");
        return ghead.next;
    }
}

static int is_asm_qualifier(struct tok *t)
{
    if (t->kind == TOK_VOLATILE || t->kind == TOK_GOTO) {
        return 1;
    }
    if (t->kind != TOK_IDENT || t->str == NULL) {
        return 0;
    }
    return strcmp(t->str, "volatile") == 0 ||
           strcmp(t->str, "__volatile__") == 0 ||
           strcmp(t->str, "__volatile") == 0 ||
           strcmp(t->str, "goto") == 0;
}

/* ---- public interface ---- */

struct node *parse(const char *src, const char *filename, struct arena *a)
{
    struct node head;
    struct node *cur_fn;
    struct node *n;
    int errors_before;

    parse_arena = a;
    type_init(a);

    /* initialize state */
    cur_scope = (struct scope *)arena_alloc(parse_arena, sizeof(struct scope));
    memset(cur_scope, 0, sizeof(struct scope));
    scope_depth = 0;
    globals = NULL;
    ntypedefs = 0;
    ntags = 0;
    nenum_vals = 0;
    label_counter = 0;
    local_offset = 0;
    brk_label = 0;
    cont_label = 0;
    cur_switch = NULL;
    static_locals = NULL;
    static_locals_tail = NULL;
    static_local_counter = 0;
    nested_funcs = NULL;
    nested_funcs_tail = NULL;
    va_list_cached = NULL;
    pushback_tok = NULL;

    /* initialize extension modules */
    attr_init(a);
    attr_set_const_eval(attr_const_expr_eval);
    asm_ext_init(a);

    /* initialize preprocessor (which initializes lexer) */
    pp_init(src, filename, a);

    /* prime the token stream */
    cur_tok = pp_next();

    head.next = NULL;
    cur_fn = &head;

    while (peek()->kind != TOK_EOF) {
        errors_before = cc_error_count;
        n = parse_toplevel();

        /* if parse_toplevel produced errors, attempt recovery */
        if (cc_error_count > errors_before) {
            skip_to_next_decl();
            continue;
        }

        while (n != NULL) {
            cur_fn->next = n;
            cur_fn = n;
            n = n->next;
            cur_fn->next = NULL;
        }
    }

    /* append static local variables to the prog list */
    if (static_locals != NULL) {
        cur_fn->next = static_locals;
        while (cur_fn->next != NULL) {
            cur_fn = cur_fn->next;
        }
    }

    /* append nested function definitions to the prog list */
    if (nested_funcs != NULL) {
        cur_fn->next = nested_funcs;
    }

    if (diag_had_errors()) {
        return NULL;
    }
    return head.next;
}

struct symbol *parse_get_globals(void)
{
    return globals;
}
