/*
 * test_pp.c - Tests for the C compiler preprocessor.
 * Pure C89. All variables at top of block.
 *
 * These tests exercise the preprocessor by feeding source text
 * through preprocessing and verifying the resulting token stream.
 * Since pp.c is not yet implemented, these tests define the
 * expected interface and behavior.
 */

#include "../test.h"
#include "free.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- stub implementations for compiler utilities ---- */

static char arena_buf[4 * 1024 * 1024];
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

    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

void err_at(const char *file, int line, int col, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: error: ", file, line, col);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/* ---- stub cc_std for lex.c ---- */
struct cc_std cc_std;

int cc_has_feat(unsigned long f)
{
    return (cc_std.feat & f) != 0;
}

int cc_has_feat2(unsigned long f)
{
    return (cc_std.feat2 & f) != 0;
}

/* ---- stub for builtin_is_known (used by pp.c) ---- */
int builtin_is_known(const char *name)
{
    /* recognize a few builtins for testing */
    if (strcmp(name, "__builtin_expect") == 0) return 1;
    if (strcmp(name, "__builtin_trap") == 0) return 1;
    if (strcmp(name, "__builtin_offsetof") == 0) return 1;
    if (strcmp(name, "__builtin_va_start") == 0) return 1;
    return 0;
}

/* stubs for globals referenced by pp.c */
int cc_target_arch;
int cc_freestanding;
int cc_function_sections;
int cc_data_sections;
int cc_general_regs_only;
int cc_nostdinc;

/* ---- forward declarations ---- */
void pp_init(const char *src, const char *filename, struct arena *a);
struct tok *pp_next(void);
void pp_add_include_path(const char *path);
void pp_add_cmdline_define(const char *def);

/* ---- helper ---- */

static void pp_setup(const char *src)
{
    arena_reset(&test_arena);
    pp_init(src, "test.c", &test_arena);
}

/* ===== #define object-like macro tests ===== */

TEST(pp_define_simple)
{
    struct tok *t;

    pp_setup("#define FOO 42\nFOO");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(pp_define_ident)
{
    struct tok *t;

    pp_setup("#define X y\nX");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "y");
}

TEST(pp_define_multiple_tokens)
{
    struct tok *t;

    pp_setup("#define PAIR 1 + 2\nPAIR");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 1);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_PLUS);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 2);
}

TEST(pp_define_no_expansion)
{
    struct tok *t;

    /* undefined identifier should pass through */
    pp_setup("abc");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "abc");
}

TEST(pp_define_redefine)
{
    struct tok *t;

    pp_setup("#define X 1\n#define X 2\nX");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 2);
}

/* ===== #ifdef / #ifndef / #endif tests ===== */

TEST(pp_ifdef_defined)
{
    struct tok *t;

    pp_setup("#define FOO\n#ifdef FOO\n42\n#endif\n");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

TEST(pp_ifdef_not_defined)
{
    struct tok *t;

    pp_setup("#ifdef FOO\n42\n#endif\n");

    /* FOO not defined, 42 should be skipped */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(pp_ifndef_not_defined)
{
    struct tok *t;

    pp_setup("#ifndef FOO\n42\n#endif\n");

    /* FOO not defined, 42 should appear */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

TEST(pp_ifndef_defined)
{
    struct tok *t;

    pp_setup("#define FOO\n#ifndef FOO\n42\n#endif\n");

    /* FOO is defined, 42 should be skipped */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(pp_ifdef_else)
{
    struct tok *t;

    pp_setup("#ifdef FOO\n1\n#else\n2\n#endif\n");

    /* FOO not defined, should get 2 from else branch */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 2);
}

TEST(pp_ifdef_else_defined)
{
    struct tok *t;

    pp_setup("#define FOO\n#ifdef FOO\n1\n#else\n2\n#endif\n");

    /* FOO defined, should get 1 from if branch */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 1);
}

/* ===== nested #if tests ===== */

TEST(pp_nested_ifdef)
{
    struct tok *t;

    pp_setup(
        "#define A\n"
        "#define B\n"
        "#ifdef A\n"
        "#ifdef B\n"
        "42\n"
        "#endif\n"
        "#endif\n");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

TEST(pp_nested_ifdef_inner_false)
{
    struct tok *t;

    pp_setup(
        "#define A\n"
        "#ifdef A\n"
        "#ifdef B\n"
        "42\n"
        "#endif\n"
        "99\n"
        "#endif\n");

    /* B not defined: 42 skipped, 99 emitted */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 99);
}

TEST(pp_nested_ifdef_outer_false)
{
    struct tok *t;

    pp_setup(
        "#ifdef A\n"
        "#ifdef B\n"
        "42\n"
        "#endif\n"
        "99\n"
        "#endif\n");

    /* A not defined: everything skipped */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(pp_nested_else)
{
    struct tok *t;

    pp_setup(
        "#ifdef A\n"
        "1\n"
        "#else\n"
        "#ifdef B\n"
        "2\n"
        "#else\n"
        "3\n"
        "#endif\n"
        "#endif\n");

    /* A not defined -> else branch; B not defined -> else branch -> 3 */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 3);
}

/* ===== function-like macro tests ===== */

TEST(pp_funcmacro_simple)
{
    struct tok *t;

    pp_setup("#define INC(x) x + 1\nINC(5)");

    /* should expand to: 5 + 1 */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 5);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_PLUS);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 1);
}

TEST(pp_funcmacro_two_args)
{
    struct tok *t;

    pp_setup("#define ADD(a, b) a + b\nADD(3, 4)");

    /* should expand to: 3 + 4 */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 3);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_PLUS);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 4);
}

TEST(pp_funcmacro_max)
{
    struct tok *t;

    /*
     * MAX(a,b) ((a) > (b) ? (a) : (b))
     * We just verify it produces some output.
     */
    pp_setup("#define MAX(a,b) ((a) > (b) ? (a) : (b))\nMAX(1, 2)");

    /* should produce at least an opening paren */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_LPAREN);
}

TEST(pp_funcmacro_no_args)
{
    struct tok *t;

    pp_setup("#define F() 42\nF()");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

TEST(pp_funcmacro_not_invoked)
{
    struct tok *t;

    /*
     * If a function-like macro name appears without parens,
     * it should not be expanded.
     */
    pp_setup("#define F(x) x\nF");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "F");
}

TEST(pp_funcmacro_stringize_preserves_name)
{
    struct tok *t;

    /*
     * Stringized arguments must not be rescanned as macro names.
     * STR(VAL) should yield the string "VAL", not the expansion of VAL.
     */
    pp_setup(
        "#define VAL 0\n"
        "#define STR(x) #x\n"
        "STR(VAL)");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_NOT_NULL(t->str);
    ASSERT_STR_EQ(t->str, "VAL");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(pp_funcmacro_stringize_preserves_escaped_string)
{
    struct tok *t;

    pp_setup(
        "#define STR(x) #x\n"
        "STR(\"a\\\\b\")");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_NOT_NULL(t->str);
    ASSERT_STR_EQ(t->str, "\"a\\\\b\"");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(pp_funcmacro_stringize_preserves_escaped_char)
{
    struct tok *t;

    pp_setup(
        "#define STR(x) #x\n"
        "STR('\\n')");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_NOT_NULL(t->str);
    ASSERT_STR_EQ(t->str, "'\\n'");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(pp_funcmacro_stringize_preserves_numeric_suffix)
{
    struct tok *t;

    pp_setup(
        "#define STR(x) #x\n"
        "STR(1ULLx)");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_NOT_NULL(t->str);
    ASSERT_STR_EQ(t->str, "1ULLx");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

/* ===== __FILE__ and __LINE__ tests ===== */

TEST(pp_builtin_file)
{
    struct tok *t;

    pp_setup("__FILE__");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_NOT_NULL(t->str);
    ASSERT_STR_EQ(t->str, "test.c");
}

TEST(pp_builtin_line)
{
    struct tok *t;

    pp_setup("__LINE__");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 1);
}

TEST(pp_builtin_line_after_newlines)
{
    struct tok *t;

    pp_setup("\n\n\n__LINE__");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 4);
}

/* ===== passthrough tests ===== */

TEST(pp_passthrough_code)
{
    struct tok *t;

    /* code without directives should pass through unchanged */
    pp_setup("int x = 5;");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_INT);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "x");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_ASSIGN);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 5);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_SEMI);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(pp_empty_input)
{
    struct tok *t;

    pp_setup("");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

/* ===== include guard pattern test ===== */

TEST(pp_include_guard_pattern)
{
    struct tok *t;

    /*
     * Typical include guard: content should appear once
     * when the guard macro is not yet defined.
     */
    pp_setup(
        "#ifndef MY_HEADER_H\n"
        "#define MY_HEADER_H\n"
        "42\n"
        "#endif\n");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

/* ===== #define with empty body ===== */

TEST(pp_define_empty)
{
    struct tok *t;

    /* define with no replacement tokens */
    pp_setup("#define EMPTY\nEMPTY 42");

    /* EMPTY expands to nothing, then we see 42 */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

/* ===== backslash-newline continuation tests (GAP-1) ===== */

TEST(pp_bsnl_define)
{
    struct tok *t;

    /* backslash-newline should join lines in #define */
    pp_setup("#define FOO 1 +\\\n2\nFOO");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 1);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_PLUS);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 2);
}

TEST(pp_bsnl_code)
{
    struct tok *t;

    /* backslash-newline in regular code */
    pp_setup("in\\\nt x;");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_INT);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "x");
}

/* ===== token pasting ## tests (GAP-9) ===== */

TEST(pp_paste_ident)
{
    struct tok *t;

    /* PASTE(var,1) should produce var1 */
    pp_setup("#define PASTE(a,b) a##b\nPASTE(var,1)");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "var1");
}

TEST(pp_paste_ident_ident)
{
    struct tok *t;

    /* PASTE(foo,bar) should produce foobar */
    pp_setup("#define PASTE(a,b) a##b\nPASTE(foo,bar)");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "foobar");
}

TEST(pp_paste_num)
{
    struct tok *t;

    /* PASTE(1,2) should produce 12 */
    pp_setup("#define PASTE(a,b) a##b\nPASTE(1,2)");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 12);
}

TEST(pp_paste_object)
{
    struct tok *t;

    /* object-like macro with ## */
    pp_setup("#define VER 1##0\nVER");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 10);
}

/* ===== __has_builtin tests ===== */

TEST(pp_has_builtin_known)
{
    struct tok *t;

    /* __builtin_expect is known, so #if should be true */
    pp_setup(
        "#if __has_builtin(__builtin_expect)\n"
        "42\n"
        "#endif\n");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

TEST(pp_has_builtin_unknown)
{
    struct tok *t;

    /* __builtin_nonexistent is not known, so #if should be false */
    pp_setup(
        "#if __has_builtin(__builtin_nonexistent)\n"
        "42\n"
        "#endif\n");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

/* ===== __has_attribute tests ===== */

TEST(pp_has_attribute_known)
{
    struct tok *t;

    pp_setup(
        "#if __has_attribute(unused)\n"
        "42\n"
        "#endif\n");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

TEST(pp_has_attribute_unknown)
{
    struct tok *t;

    pp_setup(
        "#if __has_attribute(nonexistent_attr)\n"
        "42\n"
        "#endif\n");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(pp_has_attribute_dunder)
{
    struct tok *t;

    /* __unused__ form should also match */
    pp_setup(
        "#if __has_attribute(__unused__)\n"
        "42\n"
        "#endif\n");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

/* ===== __has_feature tests ===== */

TEST(pp_has_feature_known)
{
    struct tok *t;

    pp_setup(
        "#if __has_feature(c_static_assert)\n"
        "42\n"
        "#endif\n");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

TEST(pp_has_feature_unknown)
{
    struct tok *t;

    pp_setup(
        "#if __has_feature(cxx_rvalue_references)\n"
        "42\n"
        "#endif\n");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

/* ===== ## __VA_ARGS__ comma swallowing tests ===== */

TEST(pp_va_args_comma_swallow_empty)
{
    struct tok *t;

    /*
     * #define F(fmt, ...) g(fmt, ## __VA_ARGS__)
     * F("hi") should expand to g("hi") -- comma deleted.
     */
    pp_setup(
        "#define F(fmt, ...) g(fmt, ## __VA_ARGS__)\n"
        "F(\"hi\")");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "g");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_LPAREN);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_STR_EQ(t->str, "hi");

    /* next should be ')' -- comma was swallowed */
    t = pp_next();
    ASSERT_EQ(t->kind, TOK_RPAREN);
}

TEST(pp_va_args_comma_swallow_nonempty)
{
    struct tok *t;

    /*
     * F("x=%d", x) should expand to g("x=%d", x) -- normal.
     */
    pp_setup(
        "#define F(fmt, ...) g(fmt, ## __VA_ARGS__)\n"
        "F(\"x=%d\", x)");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "g");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_LPAREN);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_STR);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_COMMA);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "x");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_RPAREN);
}

TEST(pp_va_args_basic)
{
    struct tok *t;

    /* basic __VA_ARGS__ substitution */
    pp_setup(
        "#define LOG(...) f(__VA_ARGS__)\n"
        "LOG(1, 2)");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "f");

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_LPAREN);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 1);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_COMMA);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 2);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_RPAREN);
}

/* ===== -D injection tests ===== */

TEST(pp_cmdline_define_num)
{
    struct tok *t;

    /* register -D before pp_init, verify it's defined */
    pp_add_cmdline_define("TEST_VAL=99");

    arena_reset(&test_arena);
    pp_init("TEST_VAL", "test.c", &test_arena);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 99);
}

TEST(pp_cmdline_define_bare)
{
    struct tok *t;

    /* -DFOO without value => defines as 1 */
    pp_add_cmdline_define("FOO_CMD");

    arena_reset(&test_arena);
    pp_init(
        "#ifdef FOO_CMD\n42\n#endif\n",
        "test.c", &test_arena);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

TEST(pp_assembler_guard_define)
{
    struct tok *t;

    pp_add_cmdline_define("__ASSEMBLER__=1");

    arena_reset(&test_arena);
    pp_init(
        "#ifndef __ASSEMBLER__\n"
        "#error assembler mode not enabled\n"
        "#endif\n"
        "42\n",
        "test.c", &test_arena);

    t = pp_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

int main(void)
{
    arena_init(&test_arena, arena_buf, sizeof(arena_buf));

    printf("test_pp:\n");

    /* object-like macros */
    RUN_TEST(pp_define_simple);
    RUN_TEST(pp_define_ident);
    RUN_TEST(pp_define_multiple_tokens);
    RUN_TEST(pp_define_no_expansion);
    RUN_TEST(pp_define_redefine);
    RUN_TEST(pp_define_empty);

    /* #ifdef / #ifndef / #endif */
    RUN_TEST(pp_ifdef_defined);
    RUN_TEST(pp_ifdef_not_defined);
    RUN_TEST(pp_ifndef_not_defined);
    RUN_TEST(pp_ifndef_defined);
    RUN_TEST(pp_ifdef_else);
    RUN_TEST(pp_ifdef_else_defined);

    /* nested conditionals */
    RUN_TEST(pp_nested_ifdef);
    RUN_TEST(pp_nested_ifdef_inner_false);
    RUN_TEST(pp_nested_ifdef_outer_false);
    RUN_TEST(pp_nested_else);

    /* function-like macros */
    RUN_TEST(pp_funcmacro_simple);
    RUN_TEST(pp_funcmacro_two_args);
    RUN_TEST(pp_funcmacro_max);
    RUN_TEST(pp_funcmacro_no_args);
    RUN_TEST(pp_funcmacro_not_invoked);
    RUN_TEST(pp_funcmacro_stringize_preserves_name);
    RUN_TEST(pp_funcmacro_stringize_preserves_escaped_string);
    RUN_TEST(pp_funcmacro_stringize_preserves_escaped_char);
    RUN_TEST(pp_funcmacro_stringize_preserves_numeric_suffix);

    /* built-in macros */
    RUN_TEST(pp_builtin_file);
    RUN_TEST(pp_builtin_line);
    RUN_TEST(pp_builtin_line_after_newlines);

    /* passthrough */
    RUN_TEST(pp_passthrough_code);
    RUN_TEST(pp_empty_input);

    /* include guard */
    RUN_TEST(pp_include_guard_pattern);

    /* backslash-newline continuation (GAP-1) */
    RUN_TEST(pp_bsnl_define);
    RUN_TEST(pp_bsnl_code);

    /* token pasting ## (GAP-9) */
    RUN_TEST(pp_paste_ident);
    RUN_TEST(pp_paste_ident_ident);
    RUN_TEST(pp_paste_num);
    RUN_TEST(pp_paste_object);

    /* __has_builtin */
    RUN_TEST(pp_has_builtin_known);
    RUN_TEST(pp_has_builtin_unknown);

    /* __has_attribute */
    RUN_TEST(pp_has_attribute_known);
    RUN_TEST(pp_has_attribute_unknown);
    RUN_TEST(pp_has_attribute_dunder);

    /* __has_feature */
    RUN_TEST(pp_has_feature_known);
    RUN_TEST(pp_has_feature_unknown);

    /* ## __VA_ARGS__ comma swallowing */
    RUN_TEST(pp_va_args_comma_swallow_empty);
    RUN_TEST(pp_va_args_comma_swallow_nonempty);
    RUN_TEST(pp_va_args_basic);

    /* -D injection */
    RUN_TEST(pp_cmdline_define_num);
    RUN_TEST(pp_cmdline_define_bare);
    RUN_TEST(pp_assembler_guard_define);

    TEST_SUMMARY();
    return tests_failed;
}
