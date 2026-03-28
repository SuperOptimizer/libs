/*
 * ext_attrs.c - GCC/Clang attribute and extension parsing for free-cc.
 * Parses __attribute__((...)), __typeof__, __auto_type, __extension__.
 * Pure C89. All variables declared at top of block.
 */

#include "free.h"
#include <string.h>
#include <stdio.h>

/* ---- attribute flags (bitfield) ---- */
#define ATTR_NORETURN       (1 << 0)
#define ATTR_UNUSED         (1 << 1)
#define ATTR_USED           (1 << 2)
#define ATTR_WEAK           (1 << 3)
#define ATTR_PACKED         (1 << 4)
#define ATTR_DEPRECATED     (1 << 5)
#define ATTR_NOINLINE       (1 << 6)
#define ATTR_ALWAYS_INLINE  (1 << 7)
#define ATTR_COLD           (1 << 8)
#define ATTR_HOT            (1 << 9)
#define ATTR_PURE           (1 << 10)
#define ATTR_CONST_FUNC     (1 << 11)
#define ATTR_MALLOC         (1 << 12)
#define ATTR_WARN_UNUSED    (1 << 13)
#define ATTR_CONSTRUCTOR    (1 << 14)
#define ATTR_DESTRUCTOR     (1 << 15)
#define ATTR_TRANSPARENT_U  (1 << 16)

#define VIS_DEFAULT   0
#define VIS_HIDDEN    1
#define VIS_PROTECTED 2

/* ---- attribute info ---- */
struct attr_info {
    unsigned int flags;
    int aligned;            /* 0 = not set */
    int visibility;
    int vector_size;        /* 0 = not set */
    char *alias_name;       /* NULL = not set */
    char *section_name;
    char *cleanup_func;
    char *deprecated_msg;
    int format_archetype;   /* 0=none, 1=printf, 2=scanf */
    int format_str_idx;
    int format_first_arg;
};

extern struct tok *pp_next(void);

/*
 * Callback to evaluate a constant expression using the main parser.
 * Set by parse.c via attr_set_const_eval().
 * Takes a pointer to the current token pointer, parses a full constant
 * expression (consuming tokens), writes the result to *out_val,
 * and returns 1 on success, 0 on failure.
 */
static int (*attr_eval_const_cb)(struct tok **tok_ptr, long *out_val);

void attr_set_const_eval(int (*cb)(struct tok **tok_ptr, long *out_val))
{
    attr_eval_const_cb = cb;
}

static struct arena *ext_arena;
static struct tok *ext_tok;

static struct tok *ext_peek(void) { return ext_tok; }

static struct tok *ext_advance(void)
{
    struct tok *t;
    t = ext_tok;
    ext_tok = pp_next();
    return t;
}

static void ext_expect(enum tok_kind kind, const char *msg)
{
    if (ext_tok->kind != kind)
        err_at(ext_tok->file, ext_tok->line, ext_tok->col,
               "expected %s", msg);
    ext_tok = pp_next();
}

/* skip balanced parentheses (unknown attribute args) */
static void skip_parens(void)
{
    int depth;
    depth = 1;
    ext_advance();
    while (depth > 0 && ext_peek()->kind != TOK_EOF) {
        if (ext_peek()->kind == TOK_LPAREN) depth++;
        else if (ext_peek()->kind == TOK_RPAREN) {
            depth--;
            if (depth == 0) { ext_advance(); return; }
        }
        ext_advance();
    }
}

/* parse ("string") argument */
static char *parse_string_arg(void)
{
    char *r;
    char buf[512];
    int len;
    r = NULL;
    if (ext_peek()->kind != TOK_LPAREN) return NULL;
    ext_advance();
    if (ext_peek()->kind == TOK_STR) {
        /* concatenate adjacent string literals */
        len = 0;
        while (ext_peek()->kind == TOK_STR) {
            int slen;
            slen = ext_peek()->len;
            if (len + slen < (int)sizeof(buf) - 1) {
                memcpy(buf + len, ext_peek()->str, (usize)slen);
                len += slen;
            }
            ext_advance();
        }
        buf[len] = '\0';
        r = str_dup(ext_arena, buf, len);
    }
    if (ext_peek()->kind == TOK_RPAREN) ext_advance();
    return r;
}

/*
 * parse_int_arg - parse (expr) integer argument for attributes.
 *
 * When the main parser callback is available, delegates to the full
 * expression parser so sizeof(), _Alignof(), casts, and arbitrary
 * constant expressions all work correctly. Falls back to a simplified
 * hand-written parser when the callback is not yet registered.
 */
static long parse_int_arg(void)
{
    long val;
    int depth;
    val = 0;
    if (ext_peek()->kind != TOK_LPAREN) return 0;

    /* If the main parser callback is available, use it to parse the
     * full constant expression between the parens. The callback expects
     * ext_tok to point at the '(' token. */
    if (attr_eval_const_cb) {
        if (attr_eval_const_cb(&ext_tok, &val)) {
            return val;
        }
        /* callback failed — fall through to skip balanced parens */
        depth = 0;
        while (ext_peek()->kind != TOK_EOF) {
            if (ext_peek()->kind == TOK_LPAREN) depth++;
            else if (ext_peek()->kind == TOK_RPAREN) {
                depth--;
                if (depth <= 0) { ext_advance(); return 0; }
            }
            ext_advance();
        }
        return 0;
    }

    /* Fallback: simplified parser for when callback is not available */
    ext_advance(); /* consume '(' */
    if (ext_peek()->kind == TOK_NUM) {
        val = ext_peek()->val;
        ext_advance();
    } else {
        /* skip balanced parens for unknown expressions */
        depth = 1;
        while (depth > 0 && ext_peek()->kind != TOK_EOF) {
            if (ext_peek()->kind == TOK_LPAREN) depth++;
            else if (ext_peek()->kind == TOK_RPAREN) depth--;
            if (depth > 0) ext_advance();
        }
    }
    if (ext_peek()->kind == TOK_RPAREN) ext_advance();
    return val;
}

/* helper: match attr name or __attr__ form */
static int attr_name_eq(const char *name, const char *attr)
{
    int len;
    if (strcmp(name, attr) == 0) return 1;
    /* check __attr__ form */
    len = (int)strlen(attr);
    if (name[0] == '_' && name[1] == '_' &&
        strncmp(name + 2, attr, (size_t)len) == 0 &&
        name[len + 2] == '_' && name[len + 3] == '_' &&
        name[len + 4] == '\0')
        return 1;
    return 0;
}

/* parse_format_int - parse an integer arg in format(), allowing expressions */
static int parse_format_int(void)
{
    long val;
    val = 0;
    if (ext_peek()->kind == TOK_NUM) {
        val = ext_peek()->val;
        ext_advance();
        /* handle trailing arithmetic: N+1, N-1 etc */
        while (ext_peek()->kind == TOK_PLUS ||
               ext_peek()->kind == TOK_MINUS) {
            int op;
            long rhs;
            op = (ext_peek()->kind == TOK_PLUS) ? 1 : -1;
            ext_advance();
            if (ext_peek()->kind == TOK_NUM) {
                rhs = ext_peek()->val;
                ext_advance();
                val = val + op * rhs;
            }
        }
    } else if (attr_eval_const_cb) {
        /* complex expr — try full parser if available */
        /* skip tokens until comma or rparen */
        int depth;
        depth = 0;
        while (ext_peek()->kind != TOK_EOF) {
            if (ext_peek()->kind == TOK_LPAREN) depth++;
            else if (ext_peek()->kind == TOK_RPAREN) {
                if (depth == 0) break;
                depth--;
            } else if (ext_peek()->kind == TOK_COMMA && depth == 0) {
                break;
            }
            ext_advance();
        }
    }
    return (int)val;
}

/* parse format(printf, N, M) */
static void parse_format_attr(struct attr_info *info)
{
    if (ext_peek()->kind != TOK_LPAREN) return;
    ext_advance();
    if (ext_peek()->kind == TOK_IDENT) {
        if (strcmp(ext_peek()->str, "printf") == 0 ||
            strcmp(ext_peek()->str, "__printf__") == 0)
            info->format_archetype = 1;
        else if (strcmp(ext_peek()->str, "scanf") == 0 ||
                 strcmp(ext_peek()->str, "__scanf__") == 0)
            info->format_archetype = 2;
        ext_advance();
    }
    if (ext_peek()->kind == TOK_COMMA) ext_advance();
    info->format_str_idx = parse_format_int();
    if (ext_peek()->kind == TOK_COMMA) ext_advance();
    info->format_first_arg = parse_format_int();
    if (ext_peek()->kind == TOK_RPAREN) ext_advance();
}

/* parse a single attribute name and arguments */
static void parse_single_attr(struct attr_info *info)
{
    const char *name;

    if (ext_peek()->kind == TOK_CONST) {
        info->flags |= ATTR_CONST_FUNC; ext_advance(); return;
    }
    if (ext_peek()->kind != TOK_IDENT) { ext_advance(); return; }

    /* Handle nested __attribute__((...)) from macro expansion.
     * e.g. __alloc_size__(x) expands to __attribute__((__alloc_size__(x)))
     * and if used inside another __attribute__, we see __attribute__ here. */
    if (strcmp(ext_peek()->str, "__attribute__") == 0 ||
        strcmp(ext_peek()->str, "__attribute") == 0) {
        ext_advance(); /* consume __attribute__ */
        if (ext_peek()->kind == TOK_LPAREN) {
            ext_advance(); /* ( */
            if (ext_peek()->kind == TOK_LPAREN) {
                ext_advance(); /* ( */
                while (ext_peek()->kind != TOK_RPAREN &&
                       ext_peek()->kind != TOK_EOF) {
                    parse_single_attr(info);
                    if (ext_peek()->kind == TOK_COMMA) ext_advance();
                }
                if (ext_peek()->kind == TOK_RPAREN) ext_advance();
            }
            if (ext_peek()->kind == TOK_RPAREN) ext_advance();
        }
        return;
    }

    name = ext_peek()->str;
    ext_advance();

    /* simple flag attributes */
    if (attr_name_eq(name, "noreturn"))
        info->flags |= ATTR_NORETURN;
    else if (attr_name_eq(name, "unused"))
        info->flags |= ATTR_UNUSED;
    else if (attr_name_eq(name, "used"))
        info->flags |= ATTR_USED;
    else if (attr_name_eq(name, "weak"))
        info->flags |= ATTR_WEAK;
    else if (attr_name_eq(name, "packed"))
        info->flags |= ATTR_PACKED;
    else if (attr_name_eq(name, "noinline") ||
             attr_name_eq(name, "noclone") ||
             attr_name_eq(name, "noipa"))
        info->flags |= ATTR_NOINLINE;
    else if (attr_name_eq(name, "always_inline"))
        info->flags |= ATTR_ALWAYS_INLINE;
    else if (attr_name_eq(name, "cold"))
        info->flags |= ATTR_COLD;
    else if (attr_name_eq(name, "hot"))
        info->flags |= ATTR_HOT;
    else if (attr_name_eq(name, "pure"))
        info->flags |= ATTR_PURE;
    else if (attr_name_eq(name, "const"))
        info->flags |= ATTR_CONST_FUNC;
    else if (attr_name_eq(name, "malloc")) {
        info->flags |= ATTR_MALLOC;
        if (ext_peek()->kind == TOK_LPAREN) skip_parens();
    }
    else if (attr_name_eq(name, "warn_unused_result"))
        info->flags |= ATTR_WARN_UNUSED;
    else if (attr_name_eq(name, "transparent_union"))
        info->flags |= ATTR_TRANSPARENT_U;

    /* attributes with priority arg */
    else if (attr_name_eq(name, "constructor")) {
        info->flags |= ATTR_CONSTRUCTOR;
        if (ext_peek()->kind == TOK_LPAREN) parse_int_arg();
    } else if (attr_name_eq(name, "destructor")) {
        info->flags |= ATTR_DESTRUCTOR;
        if (ext_peek()->kind == TOK_LPAREN) parse_int_arg();
    }

    /* string argument attributes */
    else if (attr_name_eq(name, "alias"))
        info->alias_name = parse_string_arg();
    else if (attr_name_eq(name, "section"))
        info->section_name = parse_string_arg();
    else if (attr_name_eq(name, "deprecated")) {
        info->flags |= ATTR_DEPRECATED;
        if (ext_peek()->kind == TOK_LPAREN)
            info->deprecated_msg = parse_string_arg();
    }

    /* cleanup(func) */
    else if (attr_name_eq(name, "cleanup")) {
        if (ext_peek()->kind == TOK_LPAREN) {
            ext_advance();
            if (ext_peek()->kind == TOK_IDENT) {
                info->cleanup_func = str_dup(ext_arena,
                    ext_peek()->str, ext_peek()->len);
                ext_advance();
            }
            if (ext_peek()->kind == TOK_RPAREN) ext_advance();
        }
    }

    /* visibility("default"|"hidden") */
    else if (attr_name_eq(name, "visibility")) {
        if (ext_peek()->kind == TOK_LPAREN) {
            ext_advance();
            if (ext_peek()->kind == TOK_STR) {
                if (strcmp(ext_peek()->str, "hidden") == 0)
                    info->visibility = VIS_HIDDEN;
                else if (strcmp(ext_peek()->str, "protected") == 0)
                    info->visibility = VIS_PROTECTED;
                else info->visibility = VIS_DEFAULT;
                ext_advance();
            }
            if (ext_peek()->kind == TOK_RPAREN) ext_advance();
        }
    }

    /* format(printf, N, M) */
    else if (attr_name_eq(name, "format"))
        parse_format_attr(info);

    /* aligned(N) */
    else if (attr_name_eq(name, "aligned")) {
        if (ext_peek()->kind == TOK_LPAREN)
            info->aligned = (int)parse_int_arg();
        else info->aligned = 16;
    }

    /* vector_size(N) */
    else if (attr_name_eq(name, "vector_size")) {
        if (ext_peek()->kind == TOK_LPAREN)
            info->vector_size = (int)parse_int_arg();
    }

    /* recognized but ignored attrs (with optional args) */
    else if (attr_name_eq(name, "nonnull") ||
             attr_name_eq(name, "sentinel") ||
             attr_name_eq(name, "nothrow") ||
             attr_name_eq(name, "leaf") ||
             attr_name_eq(name, "returns_nonnull") ||
             attr_name_eq(name, "artificial") ||
             attr_name_eq(name, "gnu_inline") ||
             attr_name_eq(name, "may_alias") ||
             attr_name_eq(name, "mode") ||
             attr_name_eq(name, "alloc_size") ||
             attr_name_eq(name, "access") ||
             attr_name_eq(name, "no_sanitize_address") ||
             attr_name_eq(name, "no_instrument_function") ||
             attr_name_eq(name, "warn_if_not_aligned") ||
             attr_name_eq(name, "fallthrough") ||
             attr_name_eq(name, "error") ||
             attr_name_eq(name, "warning") ||
             attr_name_eq(name, "externally_visible") ||
             attr_name_eq(name, "no_reorder") ||
             attr_name_eq(name, "optimize") ||
             attr_name_eq(name, "target") ||
             attr_name_eq(name, "flatten") ||
             attr_name_eq(name, "ifunc") ||
             attr_name_eq(name, "no_split_stack") ||
             attr_name_eq(name, "copy") ||
             attr_name_eq(name, "no_stack_protector") ||
             attr_name_eq(name, "assume_aligned") ||
             attr_name_eq(name, "designated_init") ||
             attr_name_eq(name, "no_sanitize") ||
             attr_name_eq(name, "no_sanitize_undefined") ||
             attr_name_eq(name, "no_profile_instrument_function")) {
        if (ext_peek()->kind == TOK_LPAREN) skip_parens();
    }

    /* unknown -- warn and skip args */
    else {
        fprintf(stderr, "%s:%d: warning: unknown attribute '%s'\n",
                ext_peek()->file ? ext_peek()->file : "<unknown>",
                ext_peek()->line, name);
        if (ext_peek()->kind == TOK_LPAREN) skip_parens();
    }
}

/* ---- public interface ---- */

void attr_init(struct arena *a) { ext_arena = a; }

void attr_info_init(struct attr_info *info)
{
    memset(info, 0, sizeof(struct attr_info));
}

void attr_parse(struct attr_info *info, struct tok **tok_ptr)
{
    ext_tok = *tok_ptr;
    ext_advance(); /* consume __attribute__ */
    ext_expect(TOK_LPAREN, "'(' after __attribute__");
    ext_expect(TOK_LPAREN, "'((' after __attribute__");
    while (ext_peek()->kind != TOK_RPAREN &&
           ext_peek()->kind != TOK_EOF) {
        parse_single_attr(info);
        if (ext_peek()->kind == TOK_COMMA) ext_advance();
    }
    if (ext_peek()->kind == TOK_RPAREN) ext_advance(); /* inner ) */
    if (ext_peek()->kind == TOK_RPAREN) ext_advance(); /* outer ) */
    *tok_ptr = ext_tok;
}

int attr_is_attribute_keyword(const struct tok *t)
{
    if (t->kind != TOK_IDENT || t->str == NULL) return 0;
    return strcmp(t->str, "__attribute__") == 0 ||
           strcmp(t->str, "__attribute") == 0;
}

int attr_is_extension_keyword(const struct tok *t)
{
    if (t->kind != TOK_IDENT || t->str == NULL) return 0;
    return strcmp(t->str, "__extension__") == 0;
}

int attr_is_typeof_keyword(const struct tok *t)
{
    if (t->kind != TOK_IDENT || t->str == NULL) return 0;
    return strcmp(t->str, "__typeof__") == 0 ||
           strcmp(t->str, "__typeof") == 0 ||
           strcmp(t->str, "typeof") == 0;
}

int attr_is_auto_type(const struct tok *t)
{
    if (t->kind != TOK_IDENT || t->str == NULL) return 0;
    return strcmp(t->str, "__auto_type") == 0;
}

int attr_skip_extension(struct tok **tok_ptr)
{
    if (!attr_is_extension_keyword(*tok_ptr)) return 0;
    ext_tok = *tok_ptr;
    ext_advance();
    *tok_ptr = ext_tok;
    return 1;
}

int attr_is_noreturn_keyword(const struct tok *t)
{
    if (t->kind != TOK_IDENT || t->str == NULL) return 0;
    return strcmp(t->str, "_Noreturn") == 0 ||
           strcmp(t->str, "__noreturn") == 0;
}

int attr_try_parse(struct attr_info *info, struct tok **tok_ptr)
{
    int parsed;
    parsed = 0;
    while (attr_is_attribute_keyword(*tok_ptr)) {
        attr_parse(info, tok_ptr);
        parsed = 1;
    }
    return parsed;
}

/* ---- apply attributes to codegen ---- */

void attr_emit_func_directives(FILE *out, const char *name,
                                const struct attr_info *info)
{
    if (info->flags & ATTR_WEAK)
        fprintf(out, "\t.weak %s\n", name);
    else
        fprintf(out, "\t.global %s\n", name);
    if (info->visibility == VIS_HIDDEN)
        fprintf(out, "\t.hidden %s\n", name);
    else if (info->visibility == VIS_PROTECTED)
        fprintf(out, "\t.protected %s\n", name);
    if (info->section_name)
        fprintf(out, "\t.section %s,\"ax\",%%progbits\n",
                info->section_name);
    if (info->alias_name)
        fprintf(out, "\t.set %s, %s\n", name, info->alias_name);
    if (info->flags & ATTR_CONSTRUCTOR) {
        fprintf(out, "\t.section .init_array,\"aw\",%%init_array\n");
        fprintf(out, "\t.p2align 3\n\t.quad %s\n\t.text\n", name);
    }
    if (info->flags & ATTR_DESTRUCTOR) {
        fprintf(out, "\t.section .fini_array,\"aw\",%%fini_array\n");
        fprintf(out, "\t.p2align 3\n\t.quad %s\n\t.text\n", name);
    }
}

void attr_emit_var_directives(FILE *out, const char *name,
                               const struct attr_info *info)
{
    if (info->flags & ATTR_WEAK)
        fprintf(out, "\t.weak %s\n", name);
    if (info->visibility == VIS_HIDDEN)
        fprintf(out, "\t.hidden %s\n", name);
    if (info->section_name)
        fprintf(out, "\t.section %s,\"aw\",%%progbits\n",
                info->section_name);
    if (info->alias_name)
        fprintf(out, "\t.global %s\n\t.set %s, %s\n",
                name, name, info->alias_name);
    if (info->aligned > 0) {
        int p2;
        p2 = 0;
        if (info->aligned >= 8) p2 = 3;
        else if (info->aligned >= 4) p2 = 2;
        else if (info->aligned >= 2) p2 = 1;
        fprintf(out, "\t.p2align %d\n", p2);
    }
}

void attr_apply_to_type(struct type *ty, const struct attr_info *info)
{
    struct member *m;
    if (ty == NULL) return;
    if (info->aligned > 0) {
        ty->align = info->aligned;
        if (ty->size > 0)
            ty->size = (ty->size + info->aligned - 1) &
                       ~(info->aligned - 1);
    }
    if (info->flags & ATTR_PACKED) {
        if (ty->kind == TY_STRUCT) {
            int off;
            off = 0;
            for (m = ty->members; m; m = m->next) {
                m->offset = off;
                off += m->ty->size;
            }
            ty->size = off;
            ty->align = 1;
        } else if (ty->kind == TY_UNION) {
            ty->align = 1;
        }
    }
    if (info->vector_size > 0 && ty->size > 0) {
        int n;
        n = info->vector_size / ty->size;
        if (n < 1) n = 1;
        ty->size = info->vector_size;
        ty->align = info->vector_size;
        ty->array_len = n;
    }
}

int attr_check_noreturn(const struct attr_info *info)
{
    return (info->flags & ATTR_NORETURN) != 0;
}

int attr_check_unused(const struct attr_info *info)
{
    return (info->flags & ATTR_UNUSED) != 0;
}

int attr_check_deprecated(const struct attr_info *info,
                           const char **msg_out)
{
    if (!(info->flags & ATTR_DEPRECATED)) return 0;
    if (msg_out) *msg_out = info->deprecated_msg;
    return 1;
}

int attr_check_warn_unused_result(const struct attr_info *info)
{
    return (info->flags & ATTR_WARN_UNUSED) != 0;
}

void attr_merge(struct attr_info *dst, const struct attr_info *src)
{
    dst->flags |= src->flags;
    if (src->aligned > 0) dst->aligned = src->aligned;
    if (src->visibility != VIS_DEFAULT)
        dst->visibility = src->visibility;
    if (src->vector_size > 0) dst->vector_size = src->vector_size;
    if (src->alias_name) dst->alias_name = src->alias_name;
    if (src->section_name) dst->section_name = src->section_name;
    if (src->cleanup_func) dst->cleanup_func = src->cleanup_func;
    if (src->deprecated_msg)
        dst->deprecated_msg = src->deprecated_msg;
    if (src->format_archetype != 0) {
        dst->format_archetype = src->format_archetype;
        dst->format_str_idx = src->format_str_idx;
        dst->format_first_arg = src->format_first_arg;
    }
}

/*
 * attr_parse_section_name - parse __attribute__((section("name")))
 * and return the section name string, or NULL if not present.
 * Consumes all __attribute__ tokens from the stream.
 */
char *attr_parse_section_name(struct tok **tok_ptr)
{
    struct attr_info info;
    attr_info_init(&info);
    attr_try_parse(&info, tok_ptr);
    return info.section_name;
}

void attr_emit_cleanup(FILE *out, const char *func, int var_offset)
{
    if (!func) return;
    fprintf(out, "\t/* cleanup(%s) */\n", func);
    fprintf(out, "\tsub x0, x29, #%d\n", var_offset);
    fprintf(out, "\tbl %s\n", func);
}
