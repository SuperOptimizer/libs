/*
 * test_setjmp.c - Tests for setjmp/longjmp.
 * Pure C89. No external dependencies.
 */

#include "../test.h"
#include <setjmp.h>

/* ===== setjmp tests ===== */

TEST(setjmp_returns_zero)
{
    jmp_buf env;
    int r;

    r = setjmp(env);
    if (r == 0) {
        /* direct call - should return 0 */
        ASSERT_EQ(r, 0);
    }
    /* if r != 0 we got here via longjmp, which is fine */
}

TEST(longjmp_returns_value)
{
    jmp_buf env;
    int r;

    r = setjmp(env);
    if (r == 0) {
        longjmp(env, 42);
        /* should not reach here */
        ASSERT(0);
    } else {
        ASSERT_EQ(r, 42);
    }
}

TEST(longjmp_zero_becomes_one)
{
    jmp_buf env;
    int r;

    r = setjmp(env);
    if (r == 0) {
        longjmp(env, 0);
        ASSERT(0);
    } else {
        /* longjmp(env, 0) must return 1 */
        ASSERT_EQ(r, 1);
    }
}

TEST(longjmp_preserves_locals)
{
    jmp_buf env;
    volatile int x;
    int r;

    x = 10;
    r = setjmp(env);
    if (r == 0) {
        x = 20;
        longjmp(env, 1);
        ASSERT(0);
    } else {
        /* volatile local should retain its modified value */
        ASSERT_EQ(x, 20);
    }
}

TEST(longjmp_negative_value)
{
    jmp_buf env;
    int r;

    r = setjmp(env);
    if (r == 0) {
        longjmp(env, -5);
        ASSERT(0);
    } else {
        ASSERT_EQ(r, -5);
    }
}

TEST(setjmp_multiple_jumps)
{
    jmp_buf env;
    volatile int count;
    int r;

    count = 0;
    r = setjmp(env);
    count++;
    if (count < 4) {
        longjmp(env, count);
    }
    /* after 3 longjmps, count should be 4 */
    ASSERT_EQ(count, 4);
    ASSERT_EQ(r, 3);
}

int main(void)
{
    printf("test_setjmp:\n");

    RUN_TEST(setjmp_returns_zero);
    RUN_TEST(longjmp_returns_value);
    RUN_TEST(longjmp_zero_becomes_one);
    RUN_TEST(longjmp_preserves_locals);
    RUN_TEST(longjmp_negative_value);
    RUN_TEST(setjmp_multiple_jumps);

    TEST_SUMMARY();
    return tests_failed;
}
