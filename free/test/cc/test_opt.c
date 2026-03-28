/*
 * test_opt.c - Peephole optimizer unit tests for the free C compiler.
 * Pure C89. All variables at top of block.
 *
 * Tests the assembly-level peephole optimizer by constructing input
 * assembly text, running it through the optimizer, and verifying the
 * output matches expected optimized form.
 *
 * The optimizer operates on textual assembly lines (AArch64 GAS syntax).
 * Each test verifies a specific optimization pattern.
 */

#include "../test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- optimizer interface (defined in src/cc/opt.c) ---- */

/*
 * opt_peephole - optimize assembly text in-place.
 * Returns the new length of the text.
 */
int opt_peephole(char *asm_text, int len);

/* ---- test helpers ---- */

#define BUF_SIZE 8192

static char out_buf[BUF_SIZE];

/*
 * run_opt - helper: copy input to out_buf, run opt_peephole in-place.
 * Returns the number of lines removed (original lines - output lines).
 */
static int run_opt(const char *input)
{
    int len;
    int new_len;
    int orig_lines;
    int new_lines;
    const char *p;

    len = (int)strlen(input);
    if (len >= BUF_SIZE - 1) {
        len = BUF_SIZE - 2;
    }
    memcpy(out_buf, input, (size_t)len);
    out_buf[len] = '\0';

    /* count original non-empty lines */
    orig_lines = 0;
    for (p = input; *p != '\0'; ) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\0' && *p != '\n') orig_lines++;
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    new_len = opt_peephole(out_buf, len);
    out_buf[new_len] = '\0';

    /* count new non-empty lines */
    new_lines = 0;
    for (p = out_buf; *p != '\0'; ) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\0' && *p != '\n') new_lines++;
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    return orig_lines - new_lines;
}

/*
 * count_lines - count non-empty lines in a string.
 */
static int count_lines(const char *s)
{
    int count;
    const char *p;

    count = 0;
    p = s;
    while (*p != '\0') {
        /* skip leading whitespace */
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p != '\0' && *p != '\n') {
            count++;
        }
        /* skip to end of line */
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return count;
}

/*
 * has_line - check if the output contains a specific line (trimmed).
 * Matches after stripping leading whitespace from each line.
 */
static int has_line(const char *haystack, const char *needle)
{
    const char *p;
    int nlen;

    nlen = (int)strlen(needle);
    p = haystack;
    while (*p != '\0') {
        /* skip leading whitespace */
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        /* compare */
        if (strncmp(p, needle, nlen) == 0) {
            /* check that line ends here (newline or end of string) */
            if (p[nlen] == '\n' || p[nlen] == '\0') {
                return 1;
            }
        }
        /* skip to next line */
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return 0;
}

/*
 * lacks_line - check that output does NOT contain a specific line.
 */
static int lacks_line(const char *haystack, const char *needle)
{
    return !has_line(haystack, needle);
}

/* ===== Test 1: Redundant load after store elimination ===== */

TEST(opt_redundant_load)
{
    /*
     * str x0, [x29, #-16]
     * ldr x0, [x29, #-16]
     * -->
     * str x0, [x29, #-16]
     * (ldr removed: x0 already holds the value)
     */
    const char *input;
    int n;

    input = "\tstr x0, [x29, #-16]\n\tldr x0, [x29, #-16]\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(has_line(out_buf, "str x0, [x29, #-16]"));
    ASSERT(lacks_line(out_buf, "ldr x0, [x29, #-16]"));
}

/* ===== Test 2: Self-move elimination ===== */

TEST(opt_self_mov)
{
    /*
     * mov x0, x0 --> (removed entirely)
     */
    const char *input;
    int n;

    input = "\tmov x0, x0\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(lacks_line(out_buf, "mov x0, x0"));
    ASSERT_EQ(count_lines(out_buf), 0);
}

/* ===== Test 3: Push-pop to mov ===== */

TEST(opt_push_pop_to_mov)
{
    /*
     * str x0, [sp, #-16]!
     * ldr x1, [sp], #16
     * -->
     * mov x1, x0
     */
    const char *input;
    int n;

    input = "\tstr x0, [sp, #-16]!\n\tldr x1, [sp], #16\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(has_line(out_buf, "mov x1, x0"));
    ASSERT(lacks_line(out_buf, "str x0, [sp, #-16]!"));
    ASSERT(lacks_line(out_buf, "ldr x1, [sp], #16"));
}

/* ===== Test 4: Add zero elimination ===== */

TEST(opt_add_zero)
{
    /*
     * add x0, x0, #0 --> (removed)
     */
    const char *input;
    int n;

    input = "\tadd x0, x0, #0\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(lacks_line(out_buf, "add x0, x0, #0"));
    ASSERT_EQ(count_lines(out_buf), 0);
}

/* ===== Test 5: Sub zero elimination ===== */

TEST(opt_sub_zero)
{
    /*
     * sub x0, x0, #0 --> (removed)
     */
    const char *input;
    int n;

    input = "\tsub x0, x0, #0\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(lacks_line(out_buf, "sub x0, x0, #0"));
    ASSERT_EQ(count_lines(out_buf), 0);
}

/* ===== Test 6: Branch to next line elimination ===== */

TEST(opt_branch_next)
{
    /*
     * b .L1
     * .L1:
     * -->
     * .L1:
     */
    const char *input;
    int n;

    input = "\tb .L1\n.L1:\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(has_line(out_buf, ".L1:"));
    ASSERT(lacks_line(out_buf, "b .L1"));
}

/* ===== Test 7: Dead store elimination (consecutive mov to same reg) ===== */

TEST(opt_dead_store)
{
    /*
     * mov x0, #5
     * mov x0, #10
     * -->
     * mov x0, #10
     */
    const char *input;
    int n;

    input = "\tmov x0, #5\n\tmov x0, #10\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(has_line(out_buf, "mov x0, #10"));
    ASSERT(lacks_line(out_buf, "mov x0, #5"));
}

/* ===== Test 8: Push-pop same register elimination ===== */

TEST(opt_push_pop_same_reg)
{
    /*
     * str x0, [sp, #-16]!
     * ldr x0, [sp], #16
     * -->
     * (both removed: push then pop same register is no-op)
     */
    const char *input;
    int n;

    input = "\tstr x0, [sp, #-16]!\n\tldr x0, [sp], #16\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(lacks_line(out_buf, "str x0, [sp, #-16]!"));
    ASSERT(lacks_line(out_buf, "ldr x0, [sp], #16"));
    ASSERT_EQ(count_lines(out_buf), 0);
}

/* ===== Test 9: Redundant load different offset preserved ===== */

TEST(opt_load_different_offset_kept)
{
    /*
     * str x0, [x29, #-16]
     * ldr x0, [x29, #-24]
     * -->
     * (both kept: different offsets, no optimization)
     */
    const char *input;
    int n;

    input = "\tstr x0, [x29, #-16]\n\tldr x0, [x29, #-24]\n";
    n = run_opt(input);

    ASSERT_EQ(n, 0);
    ASSERT(has_line(out_buf, "str x0, [x29, #-16]"));
    ASSERT(has_line(out_buf, "ldr x0, [x29, #-24]"));
}

/* ===== Test 10: Self-move with w register ===== */

TEST(opt_self_mov_w_reg)
{
    /*
     * mov w5, w5 --> kept (zero-extends upper 32 bits)
     * mov x5, x5 --> removed (true no-op)
     */
    const char *input;
    int n;

    /* w-register self-move must be kept: it clears upper 32 bits */
    input = "\tmov w5, w5\n";
    n = run_opt(input);
    ASSERT(n == 0);
    ASSERT(has_line(out_buf, "mov w5, w5"));

    /* x-register self-move is a true no-op, should be removed */
    input = "\tmov x5, x5\n";
    n = run_opt(input);
    ASSERT(n >= 1);
    ASSERT(lacks_line(out_buf, "mov x5, x5"));
}

/* ===== Test 11: Branch to next with complex label ===== */

TEST(opt_branch_next_complex_label)
{
    /*
     * b .L.end.42
     * .L.end.42:
     * -->
     * .L.end.42:
     */
    const char *input;
    int n;

    input = "\tb .L.end.42\n.L.end.42:\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(has_line(out_buf, ".L.end.42:"));
    ASSERT(lacks_line(out_buf, "b .L.end.42"));
}

/* ===== Test 12: Dead store with different destination regs preserved ===== */

TEST(opt_dead_store_different_regs)
{
    /*
     * mov x0, #5
     * mov x1, #10
     * -->
     * (both kept: different destination registers)
     */
    const char *input;
    int n;

    input = "\tmov x0, #5\n\tmov x1, #10\n";
    n = run_opt(input);

    ASSERT_EQ(n, 0);
    ASSERT(has_line(out_buf, "mov x0, #5"));
    ASSERT(has_line(out_buf, "mov x1, #10"));
}

/* ===== Test 13: Multiple optimizations in sequence ===== */

TEST(opt_multiple_patterns)
{
    /*
     * mov x0, x0        --> (removed)
     * add x1, x1, #0    --> (removed)
     * mov x2, #5
     */
    const char *input;
    int n;

    input = "\tmov x0, x0\n\tadd x1, x1, #0\n\tmov x2, #5\n";
    n = run_opt(input);

    ASSERT(n >= 2);
    ASSERT(lacks_line(out_buf, "mov x0, x0"));
    ASSERT(lacks_line(out_buf, "add x1, x1, #0"));
    ASSERT(has_line(out_buf, "mov x2, #5"));
    ASSERT_EQ(count_lines(out_buf), 1);
}

/* ===== Test 14: Redundant load after store with different regs ===== */

TEST(opt_store_load_different_reg)
{
    /*
     * str x0, [x29, #-16]
     * ldr x1, [x29, #-16]
     * -->
     * str x0, [x29, #-16]
     * mov x1, x0
     * (ldr replaced with mov: value is still in x0)
     */
    const char *input;
    int n;

    input = "\tstr x0, [x29, #-16]\n\tldr x1, [x29, #-16]\n";
    n = run_opt(input);
    (void)n; /* replacement, not removal: line count stays same */

    ASSERT(has_line(out_buf, "str x0, [x29, #-16]"));
    /* ldr should be replaced with mov */
    ASSERT(lacks_line(out_buf, "ldr x1, [x29, #-16]"));
    ASSERT(has_line(out_buf, "mov x1, x0"));
}

/* ===== Test 15: Multiply by 1 elimination ===== */

TEST(opt_mul_one)
{
    /*
     * mov x1, #1
     * mul x0, x0, x1
     * -->
     * (both removed: multiplying by 1 is identity)
     */
    const char *input;
    int n;

    input = "\tmov x1, #1\n\tmul x0, x0, x1\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(lacks_line(out_buf, "mul x0, x0, x1"));
}

/* ===== Test 16: Double branch elimination ===== */

TEST(opt_double_branch)
{
    /*
     * b .L1
     * b .L2
     * -->
     * b .L1
     * (second branch is unreachable, remove it)
     */
    const char *input;
    int n;

    input = "\tb .L1\n\tb .L2\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(has_line(out_buf, "b .L1"));
    ASSERT(lacks_line(out_buf, "b .L2"));
}

/* ===== Test 17: Add zero with w register ===== */

TEST(opt_add_zero_w_reg)
{
    /*
     * add w0, w0, #0 --> (removed)
     */
    const char *input;
    int n;

    input = "\tadd w0, w0, #0\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(lacks_line(out_buf, "add w0, w0, #0"));
}

/* ===== Test 18: No false optimization on non-zero add ===== */

TEST(opt_add_nonzero_kept)
{
    /*
     * add x0, x0, #8 --> (kept: non-zero immediate)
     */
    const char *input;
    int n;

    input = "\tadd x0, x0, #8\n";
    n = run_opt(input);

    ASSERT_EQ(n, 0);
    ASSERT(has_line(out_buf, "add x0, x0, #8"));
}

/* ===== Test 19: Push-pop across different registers ===== */

TEST(opt_push_pop_x0_x2)
{
    /*
     * str x0, [sp, #-16]!
     * ldr x2, [sp], #16
     * -->
     * mov x2, x0
     */
    const char *input;
    int n;

    input = "\tstr x0, [sp, #-16]!\n\tldr x2, [sp], #16\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(has_line(out_buf, "mov x2, x0"));
    ASSERT(lacks_line(out_buf, "str x0, [sp, #-16]!"));
    ASSERT(lacks_line(out_buf, "ldr x2, [sp], #16"));
}

/* ===== Test 20: Branch to next preserves surrounding code ===== */

TEST(opt_branch_next_context)
{
    /*
     * cmp x0, #0
     * b .L5
     * .L5:
     * mov x0, #1
     * -->
     * cmp x0, #0
     * .L5:
     * mov x0, #1
     */
    const char *input;
    int n;

    input = "\tcmp x0, #0\n\tb .L5\n.L5:\n\tmov x0, #1\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(has_line(out_buf, "cmp x0, #0"));
    ASSERT(has_line(out_buf, ".L5:"));
    ASSERT(has_line(out_buf, "mov x0, #1"));
    ASSERT(lacks_line(out_buf, "b .L5"));
}

/* ===== Test 21: Consecutive dead stores (3 movs to same reg) ===== */

TEST(opt_triple_dead_store)
{
    /*
     * mov x0, #1
     * mov x0, #2
     * mov x0, #3
     * -->
     * mov x0, #3
     */
    const char *input;
    int n;

    input = "\tmov x0, #1\n\tmov x0, #2\n\tmov x0, #3\n";
    n = run_opt(input);

    ASSERT(n >= 2);
    ASSERT(has_line(out_buf, "mov x0, #3"));
    ASSERT(lacks_line(out_buf, "mov x0, #1"));
    ASSERT(lacks_line(out_buf, "mov x0, #2"));
    ASSERT_EQ(count_lines(out_buf), 1);
}

/* ===== Test 22: No optimization on non-matching branch target ===== */

TEST(opt_branch_non_next_kept)
{
    /*
     * b .L1
     * .L2:
     * -->
     * (both kept: branch target doesn't match next label)
     */
    const char *input;
    int n;

    input = "\tb .L1\n.L2:\n";
    n = run_opt(input);

    ASSERT_EQ(n, 0);
    ASSERT(has_line(out_buf, "b .L1"));
    ASSERT(has_line(out_buf, ".L2:"));
}

/* ===== Test 23: No optimization on mov with different src/dst ===== */

TEST(opt_mov_different_regs_kept)
{
    /*
     * mov x0, x1 --> (kept: different registers, not a self-move)
     */
    const char *input;
    int n;

    input = "\tmov x0, x1\n";
    n = run_opt(input);

    ASSERT_EQ(n, 0);
    ASSERT(has_line(out_buf, "mov x0, x1"));
}

/* ===== Test 24: Sub zero with different register forms ===== */

TEST(opt_sub_zero_w_reg)
{
    /*
     * sub w3, w3, #0 --> (removed)
     */
    const char *input;
    int n;

    input = "\tsub w3, w3, #0\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(lacks_line(out_buf, "sub w3, w3, #0"));
}

/* ===== Test 25: Store-load with intervening instruction preserved ===== */

TEST(opt_store_load_intervening)
{
    /*
     * str x0, [x29, #-16]
     * mov x2, #42
     * ldr x0, [x29, #-16]
     * -->
     * str x0, [x29, #-16]
     * mov x2, #42
     * (ldr removed: x0 still holds the stored value, intervening
     *  mov x2 doesn't clobber x0 or the stored address)
     */
    const char *input;
    int n;

    input = "\tstr x0, [x29, #-16]\n\tmov x2, #42\n"
            "\tldr x0, [x29, #-16]\n";
    n = run_opt(input);

    ASSERT_EQ(n, 1);
    ASSERT(has_line(out_buf, "str x0, [x29, #-16]"));
    ASSERT(has_line(out_buf, "mov x2, #42"));
    ASSERT(lacks_line(out_buf, "ldr x0, [x29, #-16]"));
}

/* ===== Test 26: Empty input ===== */

TEST(opt_empty_input)
{
    int n;

    n = run_opt("");

    ASSERT_EQ(n, 0);
    ASSERT_EQ(count_lines(out_buf), 0);
}

/* ===== Test 27: No-op pass preserves non-optimizable code ===== */

TEST(opt_passthrough)
{
    /*
     * Normal instructions with no optimization opportunities.
     */
    const char *input;
    int n;

    input = "\tstp x29, x30, [sp, #-32]!\n"
            "\tmov x29, sp\n"
            "\tmov x0, #42\n"
            "\tldp x29, x30, [sp], #32\n"
            "\tret\n";
    n = run_opt(input);

    ASSERT_EQ(n, 0);
    ASSERT(has_line(out_buf, "stp x29, x30, [sp, #-32]!"));
    ASSERT(has_line(out_buf, "mov x29, sp"));
    ASSERT(has_line(out_buf, "mov x0, #42"));
    ASSERT(has_line(out_buf, "ldp x29, x30, [sp], #32"));
    ASSERT(has_line(out_buf, "ret"));
    ASSERT_EQ(count_lines(out_buf), 5);
}

/* ===== Test 28: Labels and directives pass through ===== */

TEST(opt_labels_preserved)
{
    const char *input;
    int n;

    input = "\t.global main\n"
            "\t.type main, %function\n"
            "main:\n"
            "\tret\n";
    n = run_opt(input);

    ASSERT_EQ(n, 0);
    ASSERT(has_line(out_buf, ".global main"));
    ASSERT(has_line(out_buf, ".type main, %function"));
    ASSERT(has_line(out_buf, "main:"));
    ASSERT(has_line(out_buf, "ret"));
}

/* ===== Test 29: Dead code after unconditional return ===== */

TEST(opt_dead_after_ret)
{
    /*
     * ret
     * mov x0, #99
     * -->
     * ret
     * (mov after ret is unreachable, but only if no label intervenes)
     */
    const char *input;
    int n;

    input = "\tret\n\tmov x0, #99\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(has_line(out_buf, "ret"));
    ASSERT(lacks_line(out_buf, "mov x0, #99"));
}

/* ===== Test 30: Dead code after ret stops at label ===== */

TEST(opt_dead_after_ret_label_stops)
{
    /*
     * ret
     * .L1:
     * mov x0, #99
     * -->
     * (all kept: label makes mov reachable via jump)
     */
    const char *input;
    int n;

    input = "\tret\n.L1:\n\tmov x0, #99\n";
    n = run_opt(input);

    ASSERT_EQ(n, 0);
    ASSERT(has_line(out_buf, "ret"));
    ASSERT(has_line(out_buf, ".L1:"));
    ASSERT(has_line(out_buf, "mov x0, #99"));
}

/* ===== Test 31: Multiple push-pop pairs ===== */

TEST(opt_multiple_push_pop)
{
    /*
     * str x0, [sp, #-16]!
     * ldr x1, [sp], #16
     * str x2, [sp, #-16]!
     * ldr x3, [sp], #16
     * -->
     * mov x1, x0
     * mov x3, x2
     */
    const char *input;
    int n;

    input = "\tstr x0, [sp, #-16]!\n\tldr x1, [sp], #16\n"
            "\tstr x2, [sp, #-16]!\n\tldr x3, [sp], #16\n";
    n = run_opt(input);

    ASSERT(n >= 2);
    ASSERT(has_line(out_buf, "mov x1, x0"));
    ASSERT(has_line(out_buf, "mov x3, x2"));
    ASSERT_EQ(count_lines(out_buf), 2);
}

/* ===== Test 32: Optimization count accuracy ===== */

TEST(opt_count_accuracy)
{
    /*
     * Three independent optimizations:
     * 1. mov x0, x0 (self-move)
     * 2. add x1, x1, #0 (add zero)
     * 3. sub x2, x2, #0 (sub zero)
     */
    const char *input;
    int n;

    input = "\tmov x0, x0\n\tadd x1, x1, #0\n\tsub x2, x2, #0\n";
    n = run_opt(input);

    ASSERT_EQ(n, 3);
    ASSERT_EQ(count_lines(out_buf), 0);
}

/* ===== Test 33: Realistic function prologue/epilogue untouched ===== */

TEST(opt_function_frame)
{
    const char *input;
    int n;

    input = "\t.global foo\n"
            "\t.type foo, %function\n"
            "\t.p2align 2\n"
            "foo:\n"
            "\tstp x29, x30, [sp, #-32]!\n"
            "\tmov x29, sp\n"
            "\tmov x0, #0\n"
            ".L.return.foo:\n"
            "\tldp x29, x30, [sp], #32\n"
            "\tret\n"
            "\t.size foo, .-foo\n";
    n = run_opt(input);

    ASSERT_EQ(n, 0);
    ASSERT(has_line(out_buf, "stp x29, x30, [sp, #-32]!"));
    ASSERT(has_line(out_buf, "ldp x29, x30, [sp], #32"));
    ASSERT(has_line(out_buf, "ret"));
}

/* ===== Test 34: Comments and blank lines preserved ===== */

TEST(opt_comments_preserved)
{
    const char *input;
    int n;

    input = "\t/* prologue */\n"
            "\tmov x0, x0\n"
            "\t/* important */\n"
            "\tmov x0, #1\n";
    n = run_opt(input);

    /* self-move should be removed, but comments kept */
    ASSERT(n >= 1);
    ASSERT(lacks_line(out_buf, "mov x0, x0"));
    ASSERT(has_line(out_buf, "mov x0, #1"));
}

/* ===== Test 35: Large realistic code block ===== */

TEST(opt_realistic_codegen)
{
    /*
     * Simulates what gen.c produces for:
     *   int x = 5; x = 10; return x;
     * which generates a dead store pattern.
     */
    const char *input;
    int n;
    int lines_before;
    int lines_after;

    input = "\t/* addr of local 'x' [fp, #-8] */\n"
            "\tsub x0, x29, #8\n"
            "\tstr x0, [sp, #-16]!\n"
            "\tmov x0, #5\n"
            "\tldr x1, [sp], #16\n"
            "\tstr x0, [x1]\n"
            "\t/* addr of local 'x' [fp, #-8] */\n"
            "\tsub x0, x29, #8\n"
            "\tstr x0, [sp, #-16]!\n"
            "\tmov x0, #10\n"
            "\tldr x1, [sp], #16\n"
            "\tstr x0, [x1]\n"
            "\t/* addr of local 'x' [fp, #-8] */\n"
            "\tsub x0, x29, #8\n"
            "\tldr x0, [x0]\n"
            "\tb .L.return.main\n";

    lines_before = count_lines(input);
    n = run_opt(input);
    lines_after = count_lines(out_buf);
    (void)n;

    /* optimizer should not increase line count */
    ASSERT(lines_after <= lines_before);
}

/* ===== Test 36: Conditional branch NOT optimized ===== */

TEST(opt_cond_branch_kept)
{
    /*
     * Conditional branches to the next label should NOT be removed
     * because the fall-through is different from the branch.
     *
     * b.eq .L1
     * .L1:
     * -->
     * (kept: conditional branch semantics differ)
     * Actually, for b.eq to next, the branch is a no-op IF taken,
     * but the condition itself matters. We keep it.
     */
    const char *input;
    int n;

    input = "\tb.eq .L1\n.L1:\n";
    n = run_opt(input);

    /* conditional branch to next is safe to remove only for unconditional */
    ASSERT_EQ(n, 0);
    ASSERT(has_line(out_buf, "b.eq .L1"));
}

/* ===== Test 37: Dead code after unconditional branch ===== */

TEST(opt_dead_after_branch)
{
    /*
     * b .L.return.main
     * mov x0, #42
     * .L.return.main:
     * -->
     * b .L.return.main
     * .L.return.main:
     * (then the branch-to-next optimization fires)
     * -->
     * .L.return.main:
     */
    const char *input;
    int n;

    input = "\tb .L.return.main\n\tmov x0, #42\n.L.return.main:\n";
    n = run_opt(input);

    ASSERT(n >= 1);
    ASSERT(has_line(out_buf, ".L.return.main:"));
    ASSERT(lacks_line(out_buf, "mov x0, #42"));
}

/* ===== main ===== */

int main(void)
{
    printf("test_opt:\n");

    /* basic single-pattern tests */
    RUN_TEST(opt_redundant_load);
    RUN_TEST(opt_self_mov);
    RUN_TEST(opt_push_pop_to_mov);
    RUN_TEST(opt_add_zero);
    RUN_TEST(opt_sub_zero);
    RUN_TEST(opt_branch_next);
    RUN_TEST(opt_dead_store);
    RUN_TEST(opt_push_pop_same_reg);

    /* negative tests (no optimization expected) */
    RUN_TEST(opt_load_different_offset_kept);
    RUN_TEST(opt_self_mov_w_reg);
    RUN_TEST(opt_branch_next_complex_label);
    RUN_TEST(opt_dead_store_different_regs);
    RUN_TEST(opt_mov_different_regs_kept);
    RUN_TEST(opt_branch_non_next_kept);
    RUN_TEST(opt_add_nonzero_kept);
    RUN_TEST(opt_store_load_intervening);

    /* multi-pattern and compound tests */
    RUN_TEST(opt_multiple_patterns);
    RUN_TEST(opt_store_load_different_reg);
    RUN_TEST(opt_mul_one);
    RUN_TEST(opt_double_branch);
    RUN_TEST(opt_add_zero_w_reg);
    RUN_TEST(opt_push_pop_x0_x2);
    RUN_TEST(opt_branch_next_context);
    RUN_TEST(opt_triple_dead_store);
    RUN_TEST(opt_sub_zero_w_reg);

    /* edge cases and preservation tests */
    RUN_TEST(opt_empty_input);
    RUN_TEST(opt_passthrough);
    RUN_TEST(opt_labels_preserved);
    RUN_TEST(opt_dead_after_ret);
    RUN_TEST(opt_dead_after_ret_label_stops);
    RUN_TEST(opt_multiple_push_pop);
    RUN_TEST(opt_count_accuracy);
    RUN_TEST(opt_function_frame);
    RUN_TEST(opt_comments_preserved);
    RUN_TEST(opt_realistic_codegen);
    RUN_TEST(opt_cond_branch_kept);
    RUN_TEST(opt_dead_after_branch);

    TEST_SUMMARY();
    return tests_failed;
}
