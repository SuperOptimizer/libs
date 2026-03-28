/*
 * type.c - Type system for the free C compiler.
 * Pure C89. All variables at top of block.
 */

#include "free.h"
#include <string.h>

/* ---- predefined types ---- */
/* fields: kind, size, align, is_unsigned, base, array_len, members,
 *         ret, params, next, name, is_vla, is_restrict, is_inline,
 *         is_flex_array, static_array_size, align_as, is_noreturn,
 *         is_atomic, is_thread_local, is_constexpr, is_variadic,
 *         is_const, is_volatile, origin */
static struct type void_type  = { TY_VOID,  0,1,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
/* aarch64 linux: plain char is unsigned */
static struct type char_type  = { TY_CHAR,  1,1,1, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type short_type = { TY_SHORT, 2,2,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type int_type   = { TY_INT,   4,4,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type long_type  = { TY_LONG,  8,8,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };

static struct type float_type  = { TY_FLOAT,  4,4,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type double_type = { TY_DOUBLE, 8,8,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
/* aarch64 linux: long double is IEEE 754 binary128 (16 bytes) */
static struct type ldouble_type = { TY_LDOUBLE, 16,16,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };

static struct type schar_type  = { TY_CHAR,  1,1,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type uchar_type  = { TY_CHAR,  1,1,1, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type ushort_type = { TY_SHORT, 2,2,1, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type uint_type   = { TY_INT,   4,4,1, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type ulong_type  = { TY_LONG,  8,8,1, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };

/* C99 predefined types */
static struct type bool_type   = { TY_BOOL,  1,1,1, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type llong_type  = { TY_LLONG, 8,8,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type ullong_type = { TY_LLONG, 8,8,1, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };

/* C99 complex types: _Complex float = 8 bytes, _Complex double = 16 bytes */
static struct type cfloat_type  = { TY_COMPLEX_FLOAT,  8, 4,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type cdouble_type = { TY_COMPLEX_DOUBLE, 16,8,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };

/* GCC __int128 extension */
static struct type int128_type  = { TY_INT128, 16,16,0, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };
static struct type uint128_type = { TY_INT128, 16,16,1, NULL,0,NULL,NULL,NULL,NULL,NULL, 0,NULL,0,0,0,0,0,0,0,0,0,0, 0,0, NULL };

struct type *ty_void  = &void_type;
struct type *ty_char  = &char_type;
struct type *ty_short = &short_type;
struct type *ty_int   = &int_type;
struct type *ty_long  = &long_type;

struct type *ty_float  = &float_type;
struct type *ty_double = &double_type;
struct type *ty_ldouble = &ldouble_type;

struct type *ty_schar  = &schar_type;
struct type *ty_uchar  = &uchar_type;
struct type *ty_ushort = &ushort_type;
struct type *ty_uint   = &uint_type;
struct type *ty_ulong  = &ulong_type;

/* C99 type pointers */
struct type *ty_bool   = &bool_type;
struct type *ty_llong  = &llong_type;
struct type *ty_ullong = &ullong_type;

/* C99 complex type pointers */
struct type *ty_cfloat  = &cfloat_type;
struct type *ty_cdouble = &cdouble_type;

/* GCC __int128 pointers */
struct type *ty_int128  = &int128_type;
struct type *ty_uint128 = &uint128_type;

/* ---- type arena ---- */
static struct arena *type_arena;

void type_init(struct arena *a)
{
    type_arena = a;

    ty_void->origin = ty_void;
    ty_char->origin = ty_char;
    ty_short->origin = ty_short;
    ty_int->origin = ty_int;
    ty_long->origin = ty_long;
    ty_float->origin = ty_float;
    ty_double->origin = ty_double;
    ty_ldouble->origin = ty_ldouble;
    ty_schar->origin = ty_schar;
    ty_uchar->origin = ty_uchar;
    ty_ushort->origin = ty_ushort;
    ty_uint->origin = ty_uint;
    ty_ulong->origin = ty_ulong;
    ty_bool->origin = ty_bool;
    ty_llong->origin = ty_llong;
    ty_ullong->origin = ty_ullong;
    ty_cfloat->origin = ty_cfloat;
    ty_cdouble->origin = ty_cdouble;
    ty_int128->origin = ty_int128;
    ty_uint128->origin = ty_uint128;
}

static struct type *type_new(enum type_kind kind, int size, int align)
{
    struct type *ty;

    ty = (struct type *)arena_alloc(type_arena, sizeof(struct type));
    memset(ty, 0, sizeof(struct type));
    ty->kind = kind;
    ty->size = size;
    ty->align = align;
    ty->origin = ty;
    return ty;
}

/* ---- constructors ---- */

struct type *type_ptr(struct type *base)
{
    struct type *ty;

    ty = type_new(TY_PTR, 8, 8);
    ty->base = base;
    return ty;
}

struct type *type_array(struct type *base, int len)
{
    struct type *ty;

    ty = type_new(TY_ARRAY, base->size * len, base->align);
    ty->base = base;
    ty->array_len = len;
    return ty;
}

struct type *type_func(struct type *ret, struct type *params)
{
    struct type *ty;

    ty = type_new(TY_FUNC, 0, 1);
    ty->ret = ret;
    ty->params = params;
    return ty;
}

struct type *type_enum(void)
{
    struct type *ty;

    ty = type_new(TY_ENUM, 4, 4);
    return ty;
}

/* ---- queries ---- */

int type_size(struct type *ty)
{
    if (ty != NULL && ty->origin != NULL) {
        ty = ty->origin;
    }
    if (ty == NULL) {
        return 0;
    }
    return ty->size;
}

int type_align(struct type *ty)
{
    if (ty != NULL && ty->origin != NULL) {
        ty = ty->origin;
    }
    if (ty == NULL) {
        return 1;
    }
    return ty->align;
}

static int type_qualifiers_match(struct type *a, struct type *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    return a->is_const == b->is_const &&
           a->is_volatile == b->is_volatile &&
           a->is_restrict == b->is_restrict &&
           a->is_atomic == b->is_atomic;
}

static int type_is_compatible_r(struct type *a, struct type *b, int top_level)
{
    if (a == b) {
        return 1;
    }
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (!top_level && !type_qualifiers_match(a, b)) {
        return 0;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    switch (a->kind) {
    case TY_VOID:
    case TY_SHORT:
    case TY_INT:
    case TY_LONG:
    case TY_FLOAT:
    case TY_DOUBLE:
    case TY_LDOUBLE:
    case TY_BOOL:
    case TY_LLONG:
    case TY_INT128:
    case TY_ATOMIC:
        return a->is_unsigned == b->is_unsigned;
    case TY_CHAR:
        /* Keep plain/signed/unsigned char distinct even through
         * qualifier copies. */
        if (a->origin != NULL && b->origin != NULL) {
            return a->origin == b->origin;
        }
        return a == b;
    case TY_PTR:
        return type_is_compatible_r(a->base, b->base, 0);
    case TY_ARRAY:
        /* arrays are compatible if element types are compatible.
         * If either has unspecified length (0), they are still
         * compatible per C standard (6.7.5.2). */
        if (a->array_len != 0 && b->array_len != 0 &&
            a->array_len != b->array_len) {
            return 0;
        }
        return type_is_compatible_r(a->base, b->base, 0);
    case TY_FUNC:
        if (!type_is_compatible_r(a->ret, b->ret, 0)) {
            return 0;
        }
        {
            struct type *pa;
            struct type *pb;

            pa = a->params;
            pb = b->params;
            while (pa && pb) {
                if (!type_is_compatible_r(pa, pb, 0)) {
                    return 0;
                }
                pa = pa->next;
                pb = pb->next;
            }
            return pa == NULL && pb == NULL;
        }
    case TY_STRUCT:
    case TY_UNION:
        /* struct/union compatibility: same name or same canonical
         * anonymous type. */
        if (a->name && b->name) {
            return strcmp(a->name, b->name) == 0;
        }
        if (a->origin != NULL && b->origin != NULL) {
            return a->origin == b->origin;
        }
        return a == b;
    case TY_ENUM:
        if (a->name && b->name) {
            return strcmp(a->name, b->name) == 0;
        }
        if (a->origin != NULL && b->origin != NULL) {
            return a->origin == b->origin;
        }
        return a == b;
    case TY_COMPLEX_FLOAT:
    case TY_COMPLEX_DOUBLE:
        /* same kind already checked above */
        return 1;
    default:
        return 0;
    }
}

int type_is_integer(struct type *ty)
{
    if (ty == NULL) {
        return 0;
    }
    return ty->kind == TY_BOOL
        || ty->kind == TY_CHAR
        || ty->kind == TY_SHORT
        || ty->kind == TY_INT
        || ty->kind == TY_LONG
        || ty->kind == TY_LLONG
        || ty->kind == TY_INT128
        || ty->kind == TY_ENUM;
}

int type_is_flonum(struct type *ty)
{
    if (ty == NULL) {
        return 0;
    }
    return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE ||
           ty->kind == TY_LDOUBLE
        || ty->kind == TY_COMPLEX_FLOAT || ty->kind == TY_COMPLEX_DOUBLE;
}

int type_is_numeric(struct type *ty)
{
    return type_is_integer(ty) || type_is_flonum(ty);
}

int type_is_pointer(struct type *ty)
{
    if (ty == NULL) {
        return 0;
    }
    return ty->kind == TY_PTR || ty->kind == TY_ARRAY;
}

int type_is_compatible(struct type *a, struct type *b)
{
    return type_is_compatible_r(a, b, 1);
}

/* ---- usual arithmetic conversions (C89 6.2.1.5) ---- */

static int type_rank(struct type *ty)
{
    if (ty == NULL) {
        return 0;
    }
    switch (ty->kind) {
    case TY_BOOL:   return 0;
    case TY_CHAR:   return 1;
    case TY_SHORT:  return 2;
    case TY_INT:    return 3;
    case TY_ENUM:   return 3;
    case TY_LONG:   return 4;
    case TY_LLONG:  return 5;
    default:        return 0;
    }
}

struct type *type_common(struct type *a, struct type *b)
{
    int ra, rb;

    if (a == NULL) return b;
    if (b == NULL) return a;

    /* complex promotion: if either operand is complex, result is complex */
    if (a->kind == TY_COMPLEX_DOUBLE || b->kind == TY_COMPLEX_DOUBLE) {
        return ty_cdouble;
    }
    if (a->kind == TY_COMPLEX_FLOAT || b->kind == TY_COMPLEX_FLOAT) {
        return ty_cfloat;
    }

    /* long double promotion */
    if (a->kind == TY_LDOUBLE || b->kind == TY_LDOUBLE) {
        return ty_ldouble;
    }

    /* float/double promotion: double > float > integer */
    if (a->kind == TY_DOUBLE || b->kind == TY_DOUBLE) {
        return ty_double;
    }
    if (a->kind == TY_FLOAT || b->kind == TY_FLOAT) {
        return ty_float;
    }

    /* integer types */
    ra = type_rank(a);
    rb = type_rank(b);

    /* same rank: if either is unsigned, result is unsigned */
    if (ra == rb) {
        if (ra < 3) {
            return ty_int;
        }
        if (a->is_unsigned || b->is_unsigned) {
            return a->is_unsigned ? a : b;
        }
        return a;
    }

    if (ra > rb) {
        /* integer promotion: everything smaller than int -> int */
        if (ra < 3) {
            return ty_int;
        }
        /* if lower-rank is unsigned and higher is signed, and
         * higher rank can represent all values, use higher rank;
         * otherwise use unsigned version of higher rank.
         * For simplicity: if unsigned rank < signed rank, use signed. */
        return a;
    } else {
        if (rb < 3) {
            return ty_int;
        }
        return b;
    }
}

/* ---- struct/union layout helpers ---- */

static int align_to(int n, int align)
{
    return (n + align - 1) & ~(align - 1);
}

void type_complete_struct(struct type *ty)
{
    struct member *m;
    int offset;
    int max_align;
    int bit_pos;       /* current bit position within storage unit */
    int unit_size;     /* size in bytes of current bitfield storage unit */
    int unit_bits;     /* size in bits of current storage unit */

    offset = 0;
    max_align = 1;
    bit_pos = 0;
    unit_size = 0;

    for (m = ty->members; m != NULL; m = m->next) {
        if (m->bit_width > 0) {
            /*
             * Named bitfield with width > 0.
             * Pack into current storage unit if it fits, else start
             * a new one.  GCC-compatible: different underlying types
             * can share a unit; the unit grows to the largest type.
             */
            int new_size;
            int new_bits;

            new_size = type_size(m->ty);
            new_bits = new_size * 8;

            if (bit_pos == 0) {
                /* starting a fresh storage unit */
                offset = align_to(offset, type_align(m->ty));
                unit_size = new_size;
                unit_bits = new_bits;
            } else {
                /* already inside a storage unit; grow if needed */
                if (new_size > unit_size) {
                    unit_size = new_size;
                    unit_bits = unit_size * 8;
                }
            }

            /* does the bitfield fit in the current unit? */
            if (bit_pos + m->bit_width > unit_bits) {
                /* close old unit, start a new one */
                offset = align_to(offset + unit_size, m->ty->align);
                bit_pos = 0;
                unit_size = new_size;
                unit_bits = new_bits;
            }

            m->offset = offset;
            m->bit_offset = bit_pos;
            bit_pos += m->bit_width;

            if (type_align(m->ty) > max_align) {
                max_align = type_align(m->ty);
            }
        } else if (m->bit_width == 0 && m->name == NULL &&
                   m->ty != NULL &&
                   m->ty->kind != TY_STRUCT &&
                   m->ty->kind != TY_UNION) {
            /*
             * Zero-width unnamed bitfield: int : 0;
             * Forces alignment to the next storage unit boundary.
             */
            if (bit_pos > 0) {
                offset += unit_size;
                bit_pos = 0;
            }
            m->offset = offset;
            m->bit_offset = 0;
            if (type_align(m->ty) > max_align) {
                max_align = type_align(m->ty);
            }
        } else {
            /* regular (non-bitfield) member */
            if (bit_pos > 0) {
                /* close out the current bitfield storage unit;
                 * advance only by the bytes actually used, so that
                 * the next member can overlap unused tail bytes
                 * (GCC-compatible behavior) */
                offset += (bit_pos + 7) / 8;
                bit_pos = 0;
            }
            offset = align_to(offset, type_align(m->ty));
            m->offset = offset;
            m->bit_offset = 0;
            offset += type_size(m->ty);
            if (type_align(m->ty) > max_align) {
                max_align = type_align(m->ty);
            }
        }
    }

    /* close any trailing bitfield storage unit;
     * advance only by the bytes actually used */
    if (bit_pos > 0) {
        offset += (bit_pos + 7) / 8;
    }

    ty->size = align_to(offset, max_align);
    ty->align = max_align;

    /* if any member is a VLA, propagate the VLA size expression
     * to the struct type so sizeof(struct) works at runtime.
     * Store the element size in size field for the sizeof handler. */
    for (m = ty->members; m != NULL; m = m->next) {
        if (m->ty && m->ty->is_vla && m->ty->vla_expr != NULL) {
            ty->vla_expr = m->ty->vla_expr;
            ty->is_vla = 1;
            /* store element size in the struct's size field;
             * sizeof handler will multiply vla_expr * size */
            if (m->ty->base != NULL) {
                ty->size = type_size(m->ty->base);
            }
            break;
        }
    }
}

void type_complete_union(struct type *ty)
{
    struct member *m;
    int max_size = 0;
    int max_align = 1;

    for (m = ty->members; m != NULL; m = m->next) {
        m->offset = 0;
        if (type_size(m->ty) > max_size) {
            max_size = type_size(m->ty);
        }
        if (type_align(m->ty) > max_align) {
            max_align = type_align(m->ty);
        }
    }
    ty->size = align_to(max_size, max_align);
    ty->align = max_align;
}

struct member *type_find_member(struct type *ty, const char *name)
{
    struct member *m;
    struct member *found;

    if (ty != NULL && ty->origin != NULL) {
        ty = ty->origin;
    }
    for (m = ty->members; m != NULL; m = m->next) {
        if (m->name && strcmp(m->name, name) == 0) {
            return m;
        }
        /* search anonymous struct/union members recursively */
        if (m->name == NULL && m->ty != NULL &&
            (m->ty->kind == TY_STRUCT || m->ty->kind == TY_UNION)) {
            found = type_find_member(m->ty, name);
            if (found != NULL) {
                /* create a synthetic member with adjusted offset */
                struct member *synth;
                synth = (struct member *)arena_alloc(
                    type_arena, sizeof(struct member));
                memset(synth, 0, sizeof(struct member));
                synth->name = found->name;
                synth->ty = found->ty;
                synth->offset = m->offset + found->offset;
                synth->bit_width = found->bit_width;
                synth->bit_offset = found->bit_offset;
                return synth;
            }
        }
    }
    return NULL;
}

struct member *type_add_member(struct type *ty, const char *name,
                               struct type *mty)
{
    struct member *m;
    struct member **pp;

    m = (struct member *)arena_alloc(type_arena, sizeof(struct member));
    memset(m, 0, sizeof(struct member));
    m->name = (char *)name;
    m->ty = mty;
    m->next = NULL;

    /* append to end of list */
    pp = &ty->members;
    while (*pp != NULL) {
        pp = &(*pp)->next;
    }
    *pp = m;
    return m;
}
