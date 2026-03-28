/*
 * test_type.c - Tests for the C compiler type system.
 * Pure C89. All variables at top of block.
 */

#include "../test.h"
#include "free.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- stub implementations for compiler utilities ---- */

static char arena_buf[64 * 1024];
static struct arena test_arena;

void arena_init(struct arena *a, char *buf, usize cap)
{
    a->buf = buf;
    a->cap = cap;
    a->used = 0;
}

void *arena_alloc(struct arena *a, usize size)
{
    void *p;

    size = (size + 7) & ~(usize)7;
    if (a->used + size > a->cap) {
        fprintf(stderr, "arena_alloc: out of memory\n");
        exit(1);
    }
    p = a->buf + a->used;
    a->used += size;
    return p;
}

void arena_reset(struct arena *a)
{
    a->used = 0;
}

int str_eq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

int str_eqn(const char *a, const char *b, int n)
{
    return strncmp(a, b, n) == 0;
}

char *str_dup(struct arena *a, const char *s, int len)
{
    char *p;

    p = (char *)arena_alloc(a, len + 1);
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

void err(const char *fmt, ...)
{
    va_list ap;
    (void)fmt;
    va_start(ap, fmt);
    va_end(ap);
    fprintf(stderr, "error\n");
    exit(1);
}

void err_at(const char *file, int line, int col, const char *fmt, ...)
{
    va_list ap;
    (void)file;
    (void)line;
    (void)col;
    (void)fmt;
    va_start(ap, fmt);
    va_end(ap);
    fprintf(stderr, "error at %s:%d:%d\n", file, line, col);
    exit(1);
}

/* ---- type system declarations ---- */
void type_init(struct arena *a);

extern struct type *ty_void;
extern struct type *ty_char;
extern struct type *ty_short;
extern struct type *ty_int;
extern struct type *ty_long;
extern struct type *ty_uchar;
extern struct type *ty_ushort;
extern struct type *ty_uint;
extern struct type *ty_ulong;

struct type *type_ptr(struct type *base);
struct type *type_array(struct type *base, int len);
struct type *type_func(struct type *ret, struct type *params);
struct type *type_enum(void);

int type_size(struct type *ty);
int type_align(struct type *ty);
int type_is_integer(struct type *ty);
int type_is_flonum(struct type *ty);
int type_is_numeric(struct type *ty);
int type_is_pointer(struct type *ty);
int type_is_compatible(struct type *a, struct type *b);
struct type *type_common(struct type *a, struct type *b);

void type_complete_struct(struct type *ty);
void type_complete_union(struct type *ty);
struct member *type_find_member(struct type *ty, const char *name);
struct member *type_add_member(struct type *ty, const char *name,
                               struct type *mty);

/* ---- helper ---- */

static void reset_arena(void)
{
    arena_reset(&test_arena);
    type_init(&test_arena);
}

/* ===== type size tests ===== */

TEST(type_size_void)
{
    ASSERT_EQ(ty_void->size, 0);
}

TEST(type_size_char)
{
    ASSERT_EQ(ty_char->size, 1);
}

TEST(type_size_short)
{
    ASSERT_EQ(ty_short->size, 2);
}

TEST(type_size_int)
{
    ASSERT_EQ(ty_int->size, 4);
}

TEST(type_size_long)
{
    ASSERT_EQ(ty_long->size, 8);
}

TEST(type_size_ptr)
{
    struct type *p;

    reset_arena();
    p = type_ptr(ty_int);
    ASSERT_EQ(p->size, 8);
}

TEST(type_size_func)
{
    ASSERT_EQ(type_size(NULL), 0);
}

/* ===== alignment tests ===== */

TEST(type_align_char)
{
    ASSERT_EQ(ty_char->align, 1);
}

TEST(type_align_short)
{
    ASSERT_EQ(ty_short->align, 2);
}

TEST(type_align_int)
{
    ASSERT_EQ(ty_int->align, 4);
}

TEST(type_align_long)
{
    ASSERT_EQ(ty_long->align, 8);
}

TEST(type_align_ptr)
{
    struct type *p;

    reset_arena();
    p = type_ptr(ty_char);
    ASSERT_EQ(p->align, 8);
}

TEST(type_align_null)
{
    ASSERT_EQ(type_align(NULL), 1);
}

/* ===== pointer type tests ===== */

TEST(type_ptr_to_int)
{
    struct type *p;

    reset_arena();
    p = type_ptr(ty_int);
    ASSERT_EQ(p->kind, TY_PTR);
    ASSERT_EQ(p->size, 8);
    ASSERT_EQ(p->align, 8);
    ASSERT(p->base == ty_int);
}

TEST(type_ptr_to_char)
{
    struct type *p;

    reset_arena();
    p = type_ptr(ty_char);
    ASSERT_EQ(p->kind, TY_PTR);
    ASSERT(p->base == ty_char);
}

TEST(type_ptr_to_ptr)
{
    struct type *p;
    struct type *pp;

    reset_arena();
    p = type_ptr(ty_int);
    pp = type_ptr(p);
    ASSERT_EQ(pp->kind, TY_PTR);
    ASSERT(pp->base == p);
    ASSERT(pp->base->base == ty_int);
    ASSERT_EQ(pp->size, 8);
}

TEST(type_ptr_to_void)
{
    struct type *p;

    reset_arena();
    p = type_ptr(ty_void);
    ASSERT_EQ(p->kind, TY_PTR);
    ASSERT(p->base == ty_void);
}

/* ===== array type tests ===== */

TEST(type_array_int_10)
{
    struct type *a;

    reset_arena();
    a = type_array(ty_int, 10);
    ASSERT_EQ(a->kind, TY_ARRAY);
    ASSERT_EQ(a->size, 40);     /* 4 * 10 */
    ASSERT_EQ(a->align, 4);
    ASSERT_EQ(a->array_len, 10);
    ASSERT(a->base == ty_int);
}

TEST(type_array_char_256)
{
    struct type *a;

    reset_arena();
    a = type_array(ty_char, 256);
    ASSERT_EQ(a->kind, TY_ARRAY);
    ASSERT_EQ(a->size, 256);    /* 1 * 256 */
    ASSERT_EQ(a->align, 1);
    ASSERT_EQ(a->array_len, 256);
}

TEST(type_array_long_5)
{
    struct type *a;

    reset_arena();
    a = type_array(ty_long, 5);
    ASSERT_EQ(a->size, 40);     /* 8 * 5 */
    ASSERT_EQ(a->align, 8);
}

TEST(type_array_of_ptrs)
{
    struct type *p;
    struct type *a;

    reset_arena();
    p = type_ptr(ty_int);
    a = type_array(p, 3);
    ASSERT_EQ(a->size, 24);     /* 8 * 3 */
    ASSERT_EQ(a->align, 8);
    ASSERT(a->base == p);
}

TEST(type_array_2d)
{
    struct type *inner;
    struct type *outer;

    reset_arena();
    inner = type_array(ty_int, 3);
    outer = type_array(inner, 4);
    ASSERT_EQ(inner->size, 12);  /* 4 * 3 */
    ASSERT_EQ(outer->size, 48); /* 12 * 4 */
}

TEST(type_array_single)
{
    struct type *a;

    reset_arena();
    a = type_array(ty_int, 1);
    ASSERT_EQ(a->size, 4);
    ASSERT_EQ(a->array_len, 1);
}

/* ===== type classification tests ===== */

TEST(type_is_integer_tests)
{
    ASSERT(type_is_integer(ty_char));
    ASSERT(type_is_integer(ty_short));
    ASSERT(type_is_integer(ty_int));
    ASSERT(type_is_integer(ty_long));
    ASSERT(!type_is_integer(ty_void));
    ASSERT(!type_is_integer(NULL));
}

TEST(type_is_integer_unsigned)
{
    ASSERT(type_is_integer(ty_uchar));
    ASSERT(type_is_integer(ty_ushort));
    ASSERT(type_is_integer(ty_uint));
    ASSERT(type_is_integer(ty_ulong));
}

TEST(type_is_integer_enum)
{
    struct type *e;

    reset_arena();
    e = type_enum();
    ASSERT(type_is_integer(e));
}

TEST(type_is_pointer_tests)
{
    struct type *p;
    struct type *a;

    reset_arena();
    p = type_ptr(ty_int);
    a = type_array(ty_int, 5);

    ASSERT(type_is_pointer(p));
    ASSERT(type_is_pointer(a));
    ASSERT(!type_is_pointer(ty_int));
    ASSERT(!type_is_pointer(ty_char));
    ASSERT(!type_is_pointer(ty_void));
    ASSERT(!type_is_pointer(NULL));
}

TEST(type_is_numeric_tests)
{
    ASSERT(type_is_numeric(ty_int));
    ASSERT(type_is_numeric(ty_char));
    ASSERT(type_is_numeric(ty_long));
    ASSERT(!type_is_numeric(ty_void));
    ASSERT(!type_is_numeric(NULL));
}

TEST(type_is_flonum_tests)
{
    ASSERT(!type_is_flonum(ty_int));
    ASSERT(!type_is_flonum(ty_char));
    ASSERT(!type_is_flonum(ty_void));
    ASSERT(!type_is_flonum(NULL));
}

/* ===== type compatibility tests ===== */

TEST(type_compat_same)
{
    ASSERT(type_is_compatible(ty_int, ty_int));
    ASSERT(type_is_compatible(ty_char, ty_char));
    ASSERT(type_is_compatible(ty_long, ty_long));
    ASSERT(type_is_compatible(ty_void, ty_void));
}

TEST(type_compat_different_kinds)
{
    ASSERT(!type_is_compatible(ty_int, ty_char));
    ASSERT(!type_is_compatible(ty_int, ty_long));
    ASSERT(!type_is_compatible(ty_char, ty_short));
}

TEST(type_compat_signed_unsigned)
{
    ASSERT(!type_is_compatible(ty_int, ty_uint));
    ASSERT(!type_is_compatible(ty_char, ty_uchar));
    ASSERT(!type_is_compatible(ty_long, ty_ulong));
}

TEST(type_compat_ptr_same_base)
{
    struct type *p1;
    struct type *p2;

    reset_arena();
    p1 = type_ptr(ty_int);
    p2 = type_ptr(ty_int);
    ASSERT(type_is_compatible(p1, p2));
}

TEST(type_compat_ptr_diff_base)
{
    struct type *p1;
    struct type *p2;

    reset_arena();
    p1 = type_ptr(ty_int);
    p2 = type_ptr(ty_char);
    ASSERT(!type_is_compatible(p1, p2));
}

TEST(type_compat_array_same)
{
    struct type *a1;
    struct type *a2;

    reset_arena();
    a1 = type_array(ty_int, 5);
    a2 = type_array(ty_int, 5);
    ASSERT(type_is_compatible(a1, a2));
}

TEST(type_compat_array_diff_len)
{
    struct type *a1;
    struct type *a2;

    reset_arena();
    a1 = type_array(ty_int, 5);
    a2 = type_array(ty_int, 10);
    ASSERT(!type_is_compatible(a1, a2));
}

TEST(type_compat_array_diff_base)
{
    struct type *a1;
    struct type *a2;

    reset_arena();
    a1 = type_array(ty_int, 5);
    a2 = type_array(ty_char, 5);
    ASSERT(!type_is_compatible(a1, a2));
}

TEST(type_compat_null)
{
    ASSERT(!type_is_compatible(NULL, ty_int));
    ASSERT(!type_is_compatible(ty_int, NULL));
    /* NULL == NULL is true in type_is_compatible (a == b check) */
    ASSERT(type_is_compatible(NULL, NULL));
}

TEST(type_compat_func)
{
    struct type *f1;
    struct type *f2;

    reset_arena();
    f1 = type_func(ty_int, NULL);
    f2 = type_func(ty_int, NULL);
    ASSERT(type_is_compatible(f1, f2));
}

TEST(type_compat_func_diff_ret)
{
    struct type *f1;
    struct type *f2;

    reset_arena();
    f1 = type_func(ty_int, NULL);
    f2 = type_func(ty_void, NULL);
    ASSERT(!type_is_compatible(f1, f2));
}

/* ===== usual arithmetic conversions tests ===== */

TEST(type_common_int_int)
{
    struct type *r;

    r = type_common(ty_int, ty_int);
    ASSERT(r == ty_int);
}

TEST(type_common_char_int)
{
    struct type *r;

    /* char promotes to int */
    r = type_common(ty_char, ty_int);
    ASSERT(r == ty_int);
}

TEST(type_common_int_long)
{
    struct type *r;

    /* int + long -> long */
    r = type_common(ty_int, ty_long);
    ASSERT(r == ty_long);
}

TEST(type_common_char_char)
{
    struct type *r;

    /* char + char -> int (integer promotion) */
    r = type_common(ty_char, ty_char);
    ASSERT(r == ty_int);
}

TEST(type_common_short_short)
{
    struct type *r;

    /* short + short -> int (integer promotion) */
    r = type_common(ty_short, ty_short);
    ASSERT(r == ty_int);
}

TEST(type_common_short_int)
{
    struct type *r;

    r = type_common(ty_short, ty_int);
    ASSERT(r == ty_int);
}

TEST(type_common_null)
{
    struct type *r;

    r = type_common(NULL, ty_int);
    ASSERT(r == ty_int);

    r = type_common(ty_int, NULL);
    ASSERT(r == ty_int);
}

TEST(type_common_long_long)
{
    struct type *r;

    r = type_common(ty_long, ty_long);
    ASSERT(r == ty_long);
}

/* ===== struct layout tests ===== */

TEST(type_struct_simple)
{
    struct type ty;
    struct member *m;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_STRUCT;

    type_add_member(&ty, "x", ty_int);
    type_add_member(&ty, "y", ty_int);
    type_complete_struct(&ty);

    ASSERT_EQ(ty.size, 8);    /* 4 + 4 */
    ASSERT_EQ(ty.align, 4);

    m = type_find_member(&ty, "x");
    ASSERT_NOT_NULL(m);
    ASSERT_EQ(m->offset, 0);

    m = type_find_member(&ty, "y");
    ASSERT_NOT_NULL(m);
    ASSERT_EQ(m->offset, 4);
}

TEST(type_struct_padding)
{
    struct type ty;
    struct member *m;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_STRUCT;

    /* char (1), then int (4) needs padding to align to 4 */
    type_add_member(&ty, "c", ty_char);
    type_add_member(&ty, "i", ty_int);
    type_complete_struct(&ty);

    /* struct should be 8 bytes: 1 byte + 3 padding + 4 bytes */
    ASSERT_EQ(ty.size, 8);
    ASSERT_EQ(ty.align, 4);

    m = type_find_member(&ty, "c");
    ASSERT_EQ(m->offset, 0);

    m = type_find_member(&ty, "i");
    ASSERT_EQ(m->offset, 4);
}

TEST(type_struct_find_missing)
{
    struct type ty;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_STRUCT;

    type_add_member(&ty, "x", ty_int);

    ASSERT_NULL(type_find_member(&ty, "y"));
    ASSERT_NULL(type_find_member(&ty, "z"));
}

TEST(type_struct_with_long)
{
    struct type ty;
    struct member *m;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_STRUCT;

    type_add_member(&ty, "a", ty_char);
    type_add_member(&ty, "b", ty_long);
    type_complete_struct(&ty);

    /* 1 byte + 7 padding + 8 bytes = 16, aligned to 8 */
    ASSERT_EQ(ty.size, 16);
    ASSERT_EQ(ty.align, 8);

    m = type_find_member(&ty, "a");
    ASSERT_EQ(m->offset, 0);

    m = type_find_member(&ty, "b");
    ASSERT_EQ(m->offset, 8);
}

TEST(type_struct_empty)
{
    struct type ty;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_STRUCT;
    type_complete_struct(&ty);

    ASSERT_EQ(ty.size, 0);
    ASSERT_EQ(ty.align, 1);
}

TEST(type_struct_single_char)
{
    struct type ty;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_STRUCT;

    type_add_member(&ty, "c", ty_char);
    type_complete_struct(&ty);

    ASSERT_EQ(ty.size, 1);
    ASSERT_EQ(ty.align, 1);
}

/* ===== union layout tests ===== */

TEST(type_union_simple)
{
    struct type ty;
    struct member *m;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_UNION;

    type_add_member(&ty, "i", ty_int);
    type_add_member(&ty, "c", ty_char);
    type_complete_union(&ty);

    /* union size = max member size = 4 */
    ASSERT_EQ(ty.size, 4);
    ASSERT_EQ(ty.align, 4);

    /* all members at offset 0 */
    m = type_find_member(&ty, "i");
    ASSERT_EQ(m->offset, 0);

    m = type_find_member(&ty, "c");
    ASSERT_EQ(m->offset, 0);
}

TEST(type_union_with_long)
{
    struct type ty;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_UNION;

    type_add_member(&ty, "a", ty_char);
    type_add_member(&ty, "b", ty_long);
    type_complete_union(&ty);

    ASSERT_EQ(ty.size, 8);
    ASSERT_EQ(ty.align, 8);
}

/* ===== function type tests ===== */

TEST(type_func_basic)
{
    struct type *f;

    reset_arena();
    f = type_func(ty_int, NULL);
    ASSERT_EQ(f->kind, TY_FUNC);
    ASSERT(f->ret == ty_int);
    ASSERT_NULL(f->params);
}

TEST(type_func_void_ret)
{
    struct type *f;

    reset_arena();
    f = type_func(ty_void, NULL);
    ASSERT(f->ret == ty_void);
}

/* ===== enum type tests ===== */

TEST(type_enum_basic)
{
    struct type *e;

    reset_arena();
    e = type_enum();
    ASSERT_EQ(e->kind, TY_ENUM);
    ASSERT_EQ(e->size, 4);
    ASSERT_EQ(e->align, 4);
}

/* ===== unsigned type tests ===== */

TEST(type_unsigned_sizes)
{
    ASSERT_EQ(ty_uchar->size, 1);
    ASSERT_EQ(ty_ushort->size, 2);
    ASSERT_EQ(ty_uint->size, 4);
    ASSERT_EQ(ty_ulong->size, 8);
}

TEST(type_unsigned_flag)
{
    ASSERT_EQ(ty_uchar->is_unsigned, 1);
    ASSERT_EQ(ty_ushort->is_unsigned, 1);
    ASSERT_EQ(ty_uint->is_unsigned, 1);
    ASSERT_EQ(ty_ulong->is_unsigned, 1);

    ASSERT_EQ(ty_char->is_unsigned, 1);  /* aarch64: plain char is unsigned */
    ASSERT_EQ(ty_int->is_unsigned, 0);
    ASSERT_EQ(ty_long->is_unsigned, 0);
}

/* ===== bitfield layout tests ===== */

TEST(type_bitfield_basic)
{
    /* struct { unsigned int type:4; unsigned int size:28; };
     * Both fit in one 4-byte unit. Total size = 4. */
    struct type ty;
    struct member *m;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_STRUCT;

    m = type_add_member(&ty, "type", ty_uint);
    m->bit_width = 4;
    m = type_add_member(&ty, "size", ty_uint);
    m->bit_width = 28;
    type_complete_struct(&ty);

    ASSERT_EQ(ty.size, 4);
    ASSERT_EQ(ty.align, 4);

    m = type_find_member(&ty, "type");
    ASSERT_EQ(m->offset, 0);
    ASSERT_EQ(m->bit_offset, 0);

    m = type_find_member(&ty, "size");
    ASSERT_EQ(m->offset, 0);
    ASSERT_EQ(m->bit_offset, 4);
}

TEST(type_bitfield_overflow)
{
    /* struct { unsigned int a:20; unsigned int b:20; };
     * a uses 20 bits of first unit. b doesn't fit (20+20=40>32),
     * so b starts in the next 4-byte unit. Total = 8. */
    struct type ty;
    struct member *m;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_STRUCT;

    m = type_add_member(&ty, "a", ty_uint);
    m->bit_width = 20;
    m = type_add_member(&ty, "b", ty_uint);
    m->bit_width = 20;
    type_complete_struct(&ty);

    ASSERT_EQ(ty.size, 8);

    m = type_find_member(&ty, "a");
    ASSERT_EQ(m->offset, 0);
    ASSERT_EQ(m->bit_offset, 0);

    m = type_find_member(&ty, "b");
    ASSERT_EQ(m->offset, 4);
    ASSERT_EQ(m->bit_offset, 0);
}

TEST(type_bitfield_mixed)
{
    /* struct { unsigned int a:4; int x; unsigned int b:8; };
     * a in first unit (4 bytes), then x at offset 4 (regular),
     * then b in new unit at offset 8. Total = 12. */
    struct type ty;
    struct member *m;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_STRUCT;

    m = type_add_member(&ty, "a", ty_uint);
    m->bit_width = 4;
    type_add_member(&ty, "x", ty_int);
    m = type_add_member(&ty, "b", ty_uint);
    m->bit_width = 8;
    type_complete_struct(&ty);

    ASSERT_EQ(ty.size, 12);

    m = type_find_member(&ty, "a");
    ASSERT_EQ(m->offset, 0);
    ASSERT_EQ(m->bit_offset, 0);

    m = type_find_member(&ty, "x");
    ASSERT_EQ(m->offset, 4);
    ASSERT_EQ(m->bit_offset, 0);

    m = type_find_member(&ty, "b");
    ASSERT_EQ(m->offset, 8);
    ASSERT_EQ(m->bit_offset, 0);
}

TEST(type_bitfield_zero_width)
{
    /* struct { unsigned int a:4; int :0; unsigned int b:8; };
     * a uses 4 bits. int:0 forces next unit boundary.
     * b starts at offset 4. Total = 8. */
    struct type ty;
    struct member *m;

    reset_arena();
    memset(&ty, 0, sizeof(ty));
    ty.kind = TY_STRUCT;

    m = type_add_member(&ty, "a", ty_uint);
    m->bit_width = 4;
    /* unnamed zero-width bitfield */
    m = type_add_member(&ty, NULL, ty_int);
    m->bit_width = 0;
    m = type_add_member(&ty, "b", ty_uint);
    m->bit_width = 8;
    type_complete_struct(&ty);

    ASSERT_EQ(ty.size, 8);

    m = type_find_member(&ty, "a");
    ASSERT_EQ(m->offset, 0);
    ASSERT_EQ(m->bit_offset, 0);

    m = type_find_member(&ty, "b");
    ASSERT_EQ(m->offset, 4);
    ASSERT_EQ(m->bit_offset, 0);
}

int main(void)
{
    arena_init(&test_arena, arena_buf, sizeof(arena_buf));

    printf("test_type:\n");

    /* sizes */
    RUN_TEST(type_size_void);
    RUN_TEST(type_size_char);
    RUN_TEST(type_size_short);
    RUN_TEST(type_size_int);
    RUN_TEST(type_size_long);
    RUN_TEST(type_size_ptr);
    RUN_TEST(type_size_func);

    /* alignment */
    RUN_TEST(type_align_char);
    RUN_TEST(type_align_short);
    RUN_TEST(type_align_int);
    RUN_TEST(type_align_long);
    RUN_TEST(type_align_ptr);
    RUN_TEST(type_align_null);

    /* pointer types */
    RUN_TEST(type_ptr_to_int);
    RUN_TEST(type_ptr_to_char);
    RUN_TEST(type_ptr_to_ptr);
    RUN_TEST(type_ptr_to_void);

    /* array types */
    RUN_TEST(type_array_int_10);
    RUN_TEST(type_array_char_256);
    RUN_TEST(type_array_long_5);
    RUN_TEST(type_array_of_ptrs);
    RUN_TEST(type_array_2d);
    RUN_TEST(type_array_single);

    /* type classification */
    RUN_TEST(type_is_integer_tests);
    RUN_TEST(type_is_integer_unsigned);
    RUN_TEST(type_is_integer_enum);
    RUN_TEST(type_is_pointer_tests);
    RUN_TEST(type_is_numeric_tests);
    RUN_TEST(type_is_flonum_tests);

    /* compatibility */
    RUN_TEST(type_compat_same);
    RUN_TEST(type_compat_different_kinds);
    RUN_TEST(type_compat_signed_unsigned);
    RUN_TEST(type_compat_ptr_same_base);
    RUN_TEST(type_compat_ptr_diff_base);
    RUN_TEST(type_compat_array_same);
    RUN_TEST(type_compat_array_diff_len);
    RUN_TEST(type_compat_array_diff_base);
    RUN_TEST(type_compat_null);
    RUN_TEST(type_compat_func);
    RUN_TEST(type_compat_func_diff_ret);

    /* usual arithmetic conversions */
    RUN_TEST(type_common_int_int);
    RUN_TEST(type_common_char_int);
    RUN_TEST(type_common_int_long);
    RUN_TEST(type_common_char_char);
    RUN_TEST(type_common_short_short);
    RUN_TEST(type_common_short_int);
    RUN_TEST(type_common_null);
    RUN_TEST(type_common_long_long);

    /* struct layout */
    RUN_TEST(type_struct_simple);
    RUN_TEST(type_struct_padding);
    RUN_TEST(type_struct_find_missing);
    RUN_TEST(type_struct_with_long);
    RUN_TEST(type_struct_empty);
    RUN_TEST(type_struct_single_char);

    /* union layout */
    RUN_TEST(type_union_simple);
    RUN_TEST(type_union_with_long);

    /* function types */
    RUN_TEST(type_func_basic);
    RUN_TEST(type_func_void_ret);

    /* enum types */
    RUN_TEST(type_enum_basic);

    /* unsigned types */
    RUN_TEST(type_unsigned_sizes);
    RUN_TEST(type_unsigned_flag);

    /* bitfield layout */
    RUN_TEST(type_bitfield_basic);
    RUN_TEST(type_bitfield_overflow);
    RUN_TEST(type_bitfield_mixed);
    RUN_TEST(type_bitfield_zero_width);

    TEST_SUMMARY();
    return tests_failed;
}
