/*
 * test_kernel_features.c - Tests for kernel-needed C language features.
 * Verifies parsing of GCC extensions used pervasively in the kernel.
 * Pure C89 test harness. All variables at top of block.
 */

#include "../test.h"
#include "free.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* arena_buf for tests (arena_init etc. are in util.c) */
static char arena_buf[512 * 1024];
static struct arena test_arena;

/* ---- stubs for cc_std and codegen globals ---- */
struct cc_std cc_std;
int cc_target_arch;
int cc_opt_level;
int cc_freestanding;
int cc_function_sections;
int cc_data_sections;
int cc_general_regs_only;
int cc_nostdinc;
int cc_pic_enabled;
int cc_debug_info;

int cc_has_feat(unsigned long f)  { return (cc_std.feat & f) != 0; }
int cc_has_feat2(unsigned long f) { return (cc_std.feat2 & f) != 0; }
int cc_std_at_least(int level)
{
    int base;
    base = cc_std.std_level;
    if (base >= STD_GNU89) base = base - STD_GNU89;
    return base >= level;
}

/* parser entry point */
extern struct node *parse(const char *src, const char *filename,
                          struct arena *a);

void cc_std_init(int level)
{
    memset(&cc_std, 0, sizeof(cc_std));
    cc_std.std_level = level;
    cc_std.feat = FEAT_LINE_COMMENTS;
    if (level >= STD_C99 || level == STD_GNU99 ||
        level == STD_GNU11 || level == STD_GNU23) {
        cc_std.feat |= FEAT_LONG_LONG | FEAT_HEX_FLOAT | FEAT_BOOL
                     | FEAT_RESTRICT | FEAT_INLINE | FEAT_UCN
                     | FEAT_MIXED_DECL | FEAT_FOR_DECL | FEAT_VLA
                     | FEAT_DESIG_INIT | FEAT_COMPOUND_LIT
                     | FEAT_FLEX_ARRAY | FEAT_STATIC_ARRAY
                     | FEAT_VARIADIC_MACRO | FEAT_PRAGMA_OP
                     | FEAT_FUNC_MACRO | FEAT_EMPTY_MACRO_ARG;
        cc_std.feat2 |= FEAT2_NO_IMPLICIT_INT;
    }
    if (level >= STD_C11 || level == STD_GNU11 || level == STD_GNU23) {
        cc_std.feat |= FEAT_ALIGNAS | FEAT_ALIGNOF | FEAT_STATIC_ASSERT
                     | FEAT_NORETURN | FEAT_GENERIC | FEAT_ATOMIC
                     | FEAT_THREAD_LOCAL | FEAT_UNICODE_STR;
        cc_std.feat2 |= FEAT2_ANON_STRUCT;
    }
    if (level >= STD_C23 || level == STD_GNU23) {
        cc_std.feat |= FEAT_BOOL_KW | FEAT_NULLPTR | FEAT_TYPEOF
                     | FEAT_BIN_LITERAL | FEAT_DIGIT_SEP
                     | FEAT_ATTR_SYNTAX;
        cc_std.feat2 |= FEAT2_CONSTEXPR | FEAT2_STATIC_ASSERT_NS
                      | FEAT2_EMPTY_INIT | FEAT2_LABEL_DECL
                      | FEAT2_UNNAMED_PARAM;
    }
    if (level == STD_GNU89) {
        cc_std.feat |= FEAT_INLINE | FEAT_LONG_LONG;
    }
    /* GNU modes always enable typeof for __typeof__ */
    if (level >= STD_GNU89) {
        cc_std.feat |= FEAT_TYPEOF;
    }
}

static void init_test(int std_level)
{
    arena_init(&test_arena, arena_buf, sizeof(arena_buf));
    cc_std_init(std_level);
}

/* ---- Test 1: typeof() in variable declarations ---- */
TEST(typeof_decl)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int f(void) { int x = 42; typeof(x) y = x; return y; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->kind, ND_FUNCDEF);
}

/* ---- Test 1b: __typeof__ (GNU spelling) ---- */
TEST(typeof_gnu)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int f(void) { int x = 1; __typeof__(x) y = x; return y; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->kind, ND_FUNCDEF);
}

/* ---- Test 1c: typeof with a type argument ---- */
TEST(typeof_type)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int f(void) { typeof(int) z = 10; return z; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
}

/* ---- Test 2: __extension__ keyword ---- */
TEST(extension_kw)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int f(void) { __extension__ int x = 42; return x; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->kind, ND_FUNCDEF);
}

/* ---- Test 3: case ranges ---- */
TEST(case_range)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int f(int x) {\n"
        "  switch(x) {\n"
        "    case 1 ... 5: return 1;\n"
        "    case 10 ... 20: return 2;\n"
        "    default: return 0;\n"
        "  }\n"
        "}\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->kind, ND_FUNCDEF);
}

/* ---- Test 4: asm labels on declarations ---- */
TEST(asm_labels)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int foo asm(\"_bar\");\n"
        "int f(void) { return foo; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
}

/* ---- Test 5: __attribute__((fallthrough)) ---- */
TEST(fallthrough)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int f(int x) {\n"
        "  switch(x) {\n"
        "    case 1:\n"
        "      x = 2;\n"
        "      __attribute__((fallthrough));\n"
        "    case 2:\n"
        "      return x;\n"
        "    default:\n"
        "      return 0;\n"
        "  }\n"
        "}\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->kind, ND_FUNCDEF);
}

/* ---- Test 7: empty struct/union ---- */
TEST(empty_struct)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "struct S {};\n"
        "int f(void) { return 0; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
}

/* ---- Test 8: forward-declared enum ---- */
TEST(fwd_enum)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "enum E;\n"
        "int f(enum E *p) { return 0; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
}

/* ---- Test 9: __builtin_types_compatible_p ---- */
TEST(types_compat)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int f(void) {\n"
        "  return __builtin_types_compatible_p(int, int);\n"
        "}\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->kind, ND_FUNCDEF);
}

/* ---- Test 10: __builtin_choose_expr ---- */
TEST(choose_expr)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int f(void) {\n"
        "  return __builtin_choose_expr(1, 42, 0);\n"
        "}\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->kind, ND_FUNCDEF);
}

/* ---- Test: __builtin_constant_p ---- */
TEST(constant_p)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int f(void) { return __builtin_constant_p(42); }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->kind, ND_FUNCDEF);
}

/* ---- Test: __builtin_expect ---- */
TEST(builtin_expect)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "int f(int x) {\n"
        "  if (__builtin_expect(x, 0)) return 1;\n"
        "  return 0;\n"
        "}\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->kind, ND_FUNCDEF);
}

/* ---- Test: __builtin_offsetof ---- */
TEST(builtin_offsetof)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "struct S { int a; int b; };\n"
        "int f(void) { return __builtin_offsetof(struct S, b); }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->kind, ND_FUNCDEF);
}

/* ---- Test: __attribute__ on struct ---- */
TEST(attr_struct)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "struct __attribute__((packed)) S { int a; char b; };\n"
        "int f(void) { return 0; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
}

/* ---- BUG-1: sizeof() inside attribute arguments ---- */

/* Test: kernel-style atomic_t with sizeof(int) in aligned attr */
TEST(attr_sizeof_aligned)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "typedef struct {\n"
        "  int counter;\n"
        "} __attribute__((__aligned__(sizeof(int)))) atomic_t;\n"
        "int f(void) { return 0; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
}

/* Test: sizeof(struct X) in attribute argument */
TEST(attr_sizeof_struct_type)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "struct Foo { int x; int y; };\n"
        "typedef int __attribute__((__aligned__(sizeof(struct Foo)))) foo_aligned;\n"
        "int f(void) { return 0; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
}

/* Test: sizeof(typedef_name) in attribute argument */
TEST(attr_sizeof_typedef)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "typedef unsigned long ulong;\n"
        "typedef int __attribute__((__aligned__(sizeof(ulong)))) aligned_t;\n"
        "int f(void) { return 0; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
}

/* Test: _Alignof, casts, and arithmetic in attribute arguments */
TEST(attr_complex_expr)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "typedef int __attribute__((__aligned__(_Alignof(long)))) a1;\n"
        "typedef int __attribute__((__aligned__(2 * sizeof(int)))) a2;\n"
        "typedef int __attribute__((__aligned__((int)4))) a3;\n"
        "typedef int __attribute__((__aligned__((sizeof(int))))) a4;\n"
        "int f(void) { return 0; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
}

/* Test: sizeof in struct member attribute */
TEST(attr_sizeof_struct_member)
{
    struct node *ast;
    init_test(STD_GNU11);
    ast = parse(
        "struct S {\n"
        "  int val __attribute__((__aligned__(sizeof(long))));\n"
        "};\n"
        "int f(void) { return 0; }\n",
        "test.c", &test_arena);
    ASSERT_NOT_NULL(ast);
}

/* ---- main ---- */
int main(void)
{
    printf("=== Kernel Feature Tests ===\n");

    RUN_TEST(typeof_decl);
    RUN_TEST(typeof_gnu);
    RUN_TEST(typeof_type);
    RUN_TEST(extension_kw);
    RUN_TEST(case_range);
    RUN_TEST(asm_labels);
    RUN_TEST(fallthrough);
    RUN_TEST(empty_struct);
    RUN_TEST(fwd_enum);
    RUN_TEST(types_compat);
    RUN_TEST(choose_expr);
    RUN_TEST(constant_p);
    RUN_TEST(builtin_expect);
    RUN_TEST(builtin_offsetof);
    RUN_TEST(attr_struct);
    RUN_TEST(attr_sizeof_aligned);
    RUN_TEST(attr_sizeof_struct_type);
    RUN_TEST(attr_sizeof_typedef);
    RUN_TEST(attr_complex_expr);
    RUN_TEST(attr_sizeof_struct_member);

    TEST_SUMMARY();
    return tests_failed > 0 ? 1 : 0;
}
