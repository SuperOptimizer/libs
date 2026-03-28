/*
 * test_gen.c - Tests for the C compiler code generator.
 * Pure C89. All variables at top of block.
 *
 * These tests exercise the code generator by feeding AST nodes
 * and checking that the output assembly contains expected
 * instructions. Since gen.c is not yet implemented, these tests
 * define the expected interface and behavior.
 */

#include "../test.h"
#include "free.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- stub implementations for compiler utilities ---- */

static char arena_buf[128 * 1024];
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

/* ---- stubs for cc_std functions (defined in cc.c) ---- */
struct cc_std cc_std;

int cc_has_feat(unsigned long f)
{
    return (cc_std.feat & f) != 0;
}

int cc_has_feat2(unsigned long f)
{
    return (cc_std.feat2 & f) != 0;
}

/* ---- stubs for pic.c, dwarf.c globals ---- */
int cc_pic_enabled;
int cc_debug_info;

void pic_emit_global_addr(FILE *f, const char *nm)
{
    (void)f; (void)nm;
}

void pic_emit_string_addr(FILE *f, int id)
{
    (void)f; (void)id;
}

void dwarf_init(FILE *f)
{
    (void)f;
}

void dwarf_generate(FILE *f)
{
    (void)f;
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

/* ---- forward declarations ---- */
struct node *parse(const char *src, const char *filename, struct arena *a);
void gen(struct node *prog, FILE *outfile);

extern struct type *ty_int;
extern struct type *ty_void;

/* ---- helpers ---- */

static char asm_buf[32 * 1024];

static int compile_to_asm(const char *src)
{
    struct node *prog;
    FILE *f;
    long len;

    arena_reset(&test_arena);
    prog = parse(src, "test.c", &test_arena);
    if (prog == NULL) {
        return 0;
    }

    f = tmpfile();
    if (f == NULL) {
        return 0;
    }

    gen(prog, f);

    len = ftell(f);
    if (len <= 0 || len >= (long)sizeof(asm_buf)) {
        fclose(f);
        return 0;
    }

    rewind(f);
    memset(asm_buf, 0, sizeof(asm_buf));
    fread(asm_buf, 1, (size_t)len, f);
    fclose(f);
    return (int)len;
}

static int asm_contains(const char *needle)
{
    return strstr(asm_buf, needle) != NULL;
}

/* ===== return constant tests ===== */

TEST(gen_return_42)
{
    int n;

    n = compile_to_asm("int main(void) { return 42; }");
    ASSERT(n > 0);

    /* output should contain a ret instruction */
    ASSERT(asm_contains("ret"));
}

TEST(gen_return_zero)
{
    int n;

    n = compile_to_asm("int main(void) { return 0; }");
    ASSERT(n > 0);
    ASSERT(asm_contains("ret"));
}

/* ===== function prologue/epilogue tests ===== */

TEST(gen_prologue_stp)
{
    int n;

    /* use a non-leaf function so prologue is emitted */
    n = compile_to_asm("int foo(void); int main(void) { return foo(); }");
    ASSERT(n > 0);

    /* AArch64 function prologue saves frame pointer and link register */
    ASSERT(asm_contains("stp"));
}

TEST(gen_epilogue_ldp)
{
    int n;

    /* use a non-leaf function so epilogue is emitted */
    n = compile_to_asm("int foo(void); int main(void) { return foo(); }");
    ASSERT(n > 0);

    /* AArch64 epilogue restores with ldp */
    ASSERT(asm_contains("ldp"));
}

TEST(gen_function_label)
{
    int n;

    n = compile_to_asm("int main(void) { return 0; }");
    ASSERT(n > 0);

    /* function should have a label */
    ASSERT(asm_contains("main"));
}

/* ===== arithmetic instruction tests ===== */

TEST(gen_add_instruction)
{
    int n;

    /* use parameter to prevent constant folding */
    n = compile_to_asm("int f(int a) { return a + 2; }");
    ASSERT(n > 0);

    /* should contain an add instruction */
    ASSERT(asm_contains("add"));
}

TEST(gen_sub_instruction)
{
    int n;

    n = compile_to_asm("int f(int a) { return a - 3; }");
    ASSERT(n > 0);

    /* should contain a sub instruction */
    ASSERT(asm_contains("sub"));
}

TEST(gen_mul_instruction)
{
    int n;

    n = compile_to_asm("int f(int a) { return a * 4; }");
    ASSERT(n > 0);

    /* should contain a mul instruction */
    ASSERT(asm_contains("mul"));
}

TEST(gen_div_instruction)
{
    int n;

    n = compile_to_asm("int f(int a) { return a / 2; }");
    ASSERT(n > 0);

    /* should contain sdiv (signed divide on AArch64) */
    ASSERT(asm_contains("sdiv"));
}

/* ===== mov instruction tests ===== */

TEST(gen_mov_immediate)
{
    int n;

    n = compile_to_asm("int f(void) { return 42; }");
    ASSERT(n > 0);

    /* loading a constant should use mov */
    ASSERT(asm_contains("mov"));
}

/* ===== comparison instruction tests ===== */

TEST(gen_cmp_instruction)
{
    int n;

    n = compile_to_asm("int f(int a) { return a == 2; }");
    ASSERT(n > 0);

    /* comparison should use cmp */
    ASSERT(asm_contains("cmp"));
}

/* ===== multiple functions test ===== */

TEST(gen_two_functions)
{
    int n;

    n = compile_to_asm(
        "int foo(void) { return 1; }\n"
        "int bar(void) { return 2; }\n");
    ASSERT(n > 0);

    /* both function labels should appear */
    ASSERT(asm_contains("foo"));
    ASSERT(asm_contains("bar"));
}

/* ===== global directive test ===== */

TEST(gen_global_directive)
{
    int n;

    n = compile_to_asm("int main(void) { return 0; }");
    ASSERT(n > 0);

    /* function should be exported with .global or .globl */
    ASSERT(asm_contains(".glob"));
}

/* ===== output non-empty test ===== */

TEST(gen_output_nonempty)
{
    int n;

    n = compile_to_asm("int f(void) { return 0; }");
    ASSERT(n > 0);
    ASSERT(asm_buf[0] != '\0');
}

/* ===== text section test ===== */

TEST(gen_text_section)
{
    int n;

    n = compile_to_asm("int f(void) { return 0; }");
    ASSERT(n > 0);

    /* should contain .text directive */
    ASSERT(asm_contains(".text"));
}

/* ===== atomic builtin tests ===== */

TEST(gen_atomic_load_relaxed)
{
    int n;

    n = compile_to_asm(
        "long f(long *p) { return __atomic_load_n(p, 0); }");
    ASSERT(n > 0);
    /* relaxed load uses plain ldr, not ldar */
    ASSERT(asm_contains("ldr x0, [x0]"));
}

TEST(gen_atomic_load_acquire)
{
    int n;

    n = compile_to_asm(
        "long f(long *p) { return __atomic_load_n(p, 2); }");
    ASSERT(n > 0);
    /* acquire load uses ldar */
    ASSERT(asm_contains("ldar x0, [x0]"));
}

TEST(gen_atomic_store_relaxed)
{
    int n;

    n = compile_to_asm(
        "void f(long *p, long v)"
        "{ __atomic_store_n(p, v, 0); }");
    ASSERT(n > 0);
    /* relaxed store uses plain str, not stlr */
    ASSERT(asm_contains("str x1, [x0]"));
}

TEST(gen_atomic_store_release)
{
    int n;

    n = compile_to_asm(
        "void f(long *p, long v)"
        "{ __atomic_store_n(p, v, 3); }");
    ASSERT(n > 0);
    /* release store uses stlr */
    ASSERT(asm_contains("stlr x1, [x0]"));
}

TEST(gen_atomic_exchange)
{
    int n;

    n = compile_to_asm(
        "long f(long *p, long v)"
        "{ return __atomic_exchange_n(p, v, 5); }");
    ASSERT(n > 0);
    /* seq_cst exchange uses ldaxr/stlxr loop */
    ASSERT(asm_contains("ldaxr"));
    ASSERT(asm_contains("stlxr"));
    ASSERT(asm_contains("cbnz"));
}

TEST(gen_atomic_add_fetch)
{
    int n;

    n = compile_to_asm(
        "long f(long *p, long v)"
        "{ return __atomic_add_fetch(p, v, 5); }");
    ASSERT(n > 0);
    /* should have ldaxr, add, stlxr loop */
    ASSERT(asm_contains("ldaxr"));
    ASSERT(asm_contains("add x2, x2, x1"));
    ASSERT(asm_contains("stlxr"));
}

TEST(gen_atomic_fetch_add)
{
    int n;

    n = compile_to_asm(
        "long f(long *p, long v)"
        "{ return __atomic_fetch_add(p, v, 5); }");
    ASSERT(n > 0);
    /* should have ldaxr, add into separate reg, stlxr */
    ASSERT(asm_contains("ldaxr"));
    ASSERT(asm_contains("add x4, x2, x1"));
    ASSERT(asm_contains("stlxr"));
    /* returns old value */
    ASSERT(asm_contains("mov x0, x2"));
}

TEST(gen_sync_synchronize)
{
    int n;

    n = compile_to_asm(
        "void f(void) { __sync_synchronize(); }");
    ASSERT(n > 0);
    ASSERT(asm_contains("dmb ish"));
}

int main(void)
{
    arena_init(&test_arena, arena_buf, sizeof(arena_buf));

    printf("test_gen:\n");

    /* return constants */
    RUN_TEST(gen_return_42);
    RUN_TEST(gen_return_zero);

    /* prologue/epilogue */
    RUN_TEST(gen_prologue_stp);
    RUN_TEST(gen_epilogue_ldp);
    RUN_TEST(gen_function_label);

    /* arithmetic */
    RUN_TEST(gen_add_instruction);
    RUN_TEST(gen_sub_instruction);
    RUN_TEST(gen_mul_instruction);
    RUN_TEST(gen_div_instruction);

    /* mov */
    RUN_TEST(gen_mov_immediate);

    /* comparison */
    RUN_TEST(gen_cmp_instruction);

    /* multiple functions */
    RUN_TEST(gen_two_functions);

    /* directives */
    RUN_TEST(gen_global_directive);
    RUN_TEST(gen_output_nonempty);
    RUN_TEST(gen_text_section);

    /* atomics */
    RUN_TEST(gen_atomic_load_relaxed);
    RUN_TEST(gen_atomic_load_acquire);
    RUN_TEST(gen_atomic_store_relaxed);
    RUN_TEST(gen_atomic_store_release);
    RUN_TEST(gen_atomic_exchange);
    RUN_TEST(gen_atomic_add_fetch);
    RUN_TEST(gen_atomic_fetch_add);
    RUN_TEST(gen_sync_synchronize);

    TEST_SUMMARY();
    return tests_failed;
}
int cc_target_arch = 0;
int cc_freestanding = 0;
int cc_function_sections = 0;
int cc_data_sections = 0;
int cc_general_regs_only = 0;
int cc_nostdinc = 0;
/* builtin_is_known now linked from ext_builtins.c */
/* asm_is_asm_keyword provided by ext_asm.c */
int attr_is_attribute_keyword(const char *s) { (void)s; return 0; }
int attr_is_extension_keyword(const char *s) { (void)s; return 0; }
int attr_is_typeof_keyword(const char *s) { (void)s; return 0; }
int attr_is_auto_type(const char *s) { (void)s; return 0; }
int attr_is_noreturn_keyword(const char *s) { (void)s; return 0; }
void attr_init(void) {}
void attr_parse(void) {}
void attr_info_init(void *info) { memset(info, 0, 64); }
int attr_try_parse(void *info, void *tok_ptr) {
    (void)info; (void)tok_ptr; return 0;
}
void attr_apply_to_type(void *ty, const void *info) {
    (void)ty; (void)info;
}
char *attr_parse_section_name(void *t) { (void)t; return 0; }
void attr_set_const_eval(void *cb) { (void)cb; }
/* asm_ext_init provided by ext_asm.c */
