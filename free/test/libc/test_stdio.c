/*
 * test_stdio.c - Tests for stdio.h formatting functions.
 * Pure C89. No external dependencies.
 */

#include "../test.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

/*
 * snprintf is C99 but part of the free libc API.
 * Declare it explicitly for C89 mode compilation.
 */
int snprintf(char *str, size_t size, const char *fmt, ...);

/* ===== snprintf %d tests ===== */

TEST(snprintf_d_positive)
{
    char buf[64];
    int ret;

    ret = snprintf(buf, sizeof(buf), "%d", 42);
    ASSERT_STR_EQ(buf, "42");
    ASSERT_EQ(ret, 2);
}

TEST(snprintf_d_negative)
{
    char buf[64];
    int ret;

    ret = snprintf(buf, sizeof(buf), "%d", -42);
    ASSERT_STR_EQ(buf, "-42");
    ASSERT_EQ(ret, 3);
}

TEST(snprintf_d_zero)
{
    char buf[64];
    int ret;

    ret = snprintf(buf, sizeof(buf), "%d", 0);
    ASSERT_STR_EQ(buf, "0");
    ASSERT_EQ(ret, 1);
}

TEST(snprintf_d_large)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%d", 2147483647);
    ASSERT_STR_EQ(buf, "2147483647");
}

TEST(snprintf_d_negative_large)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%d", -2147483647);
    ASSERT_STR_EQ(buf, "-2147483647");
}

/* ===== snprintf %s tests ===== */

TEST(snprintf_s_basic)
{
    char buf[64];
    int ret;

    ret = snprintf(buf, sizeof(buf), "%s", "hello");
    ASSERT_STR_EQ(buf, "hello");
    ASSERT_EQ(ret, 5);
}

TEST(snprintf_s_empty)
{
    char buf[64];
    int ret;

    ret = snprintf(buf, sizeof(buf), "%s", "");
    ASSERT_STR_EQ(buf, "");
    ASSERT_EQ(ret, 0);
}

TEST(snprintf_s_with_text)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "<%s>", "test");
    ASSERT_STR_EQ(buf, "<test>");
}

/* ===== snprintf %x tests ===== */

TEST(snprintf_x_basic)
{
    char buf[64];
    int ret;

    ret = snprintf(buf, sizeof(buf), "%x", 255);
    ASSERT_STR_EQ(buf, "ff");
    ASSERT_EQ(ret, 2);
}

TEST(snprintf_x_zero)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%x", 0);
    ASSERT_STR_EQ(buf, "0");
}

TEST(snprintf_x_large)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%x", 0xDEAD);
    ASSERT_STR_EQ(buf, "dead");
}

TEST(snprintf_x_one)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%x", 1);
    ASSERT_STR_EQ(buf, "1");
}

/* ===== snprintf %c tests ===== */

TEST(snprintf_c_basic)
{
    char buf[64];
    int ret;

    ret = snprintf(buf, sizeof(buf), "%c", 'A');
    ASSERT_EQ(buf[0], 'A');
    ASSERT_EQ(ret, 1);
}

TEST(snprintf_c_space)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%c", ' ');
    ASSERT_EQ(buf[0], ' ');
}

TEST(snprintf_c_in_string)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "[%c]", 'X');
    ASSERT_STR_EQ(buf, "[X]");
}

/* ===== snprintf %p tests ===== */

TEST(snprintf_p_nonnull)
{
    char buf[64];
    int dummy;
    int ret;

    ret = snprintf(buf, sizeof(buf), "%p", (void *)&dummy);
    /* should produce some non-empty string starting with 0x or a hex address */
    ASSERT(ret > 0);
    ASSERT(strlen(buf) > 0);
}

TEST(snprintf_p_null)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%p", (void *)0);
    /* output is implementation-defined but should be non-empty */
    ASSERT(strlen(buf) > 0);
}

/* ===== snprintf %% tests ===== */

TEST(snprintf_percent_literal)
{
    char buf[64];
    int ret;

    ret = snprintf(buf, sizeof(buf), "%%");
    ASSERT_STR_EQ(buf, "%");
    ASSERT_EQ(ret, 1);
}

TEST(snprintf_percent_in_string)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "100%%");
    ASSERT_STR_EQ(buf, "100%");
}

TEST(snprintf_percent_multiple)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "a%%b%%c");
    ASSERT_STR_EQ(buf, "a%b%c");
}

/* ===== snprintf field width tests ===== */

TEST(snprintf_width_d)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%10d", 42);
    ASSERT_STR_EQ(buf, "        42");
    ASSERT_EQ((long)strlen(buf), 10);
}

TEST(snprintf_width_d_negative)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%10d", -42);
    ASSERT_STR_EQ(buf, "       -42");
    ASSERT_EQ((long)strlen(buf), 10);
}

TEST(snprintf_width_s)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%10s", "hi");
    ASSERT_STR_EQ(buf, "        hi");
    ASSERT_EQ((long)strlen(buf), 10);
}

TEST(snprintf_width_smaller_than_value)
{
    char buf[64];

    /* width smaller than value: value should not be truncated */
    snprintf(buf, sizeof(buf), "%2d", 12345);
    ASSERT_STR_EQ(buf, "12345");
}

/* ===== snprintf zero padding tests ===== */

TEST(snprintf_zero_pad_d)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%05d", 42);
    ASSERT_STR_EQ(buf, "00042");
}

TEST(snprintf_zero_pad_d_negative)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%05d", -42);
    ASSERT_STR_EQ(buf, "-0042");
}

TEST(snprintf_zero_pad_x)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%08x", 0xFF);
    ASSERT_STR_EQ(buf, "000000ff");
}

TEST(snprintf_zero_pad_width_1)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%01d", 5);
    ASSERT_STR_EQ(buf, "5");
}

/* ===== snprintf truncation tests ===== */

TEST(snprintf_truncation)
{
    char buf[4];
    int ret;

    ret = snprintf(buf, sizeof(buf), "hello");
    /* should write "hel\0" */
    ASSERT_STR_EQ(buf, "hel");
    /* return value should be what WOULD have been written */
    ASSERT_EQ(ret, 5);
}

TEST(snprintf_exact_fit)
{
    char buf[6];
    int ret;

    ret = snprintf(buf, sizeof(buf), "hello");
    ASSERT_STR_EQ(buf, "hello");
    ASSERT_EQ(ret, 5);
}

TEST(snprintf_size_zero)
{
    char buf[4];
    int ret;

    strcpy(buf, "xxx");
    ret = snprintf(buf, 0, "hello");
    /* should not write anything */
    ASSERT_STR_EQ(buf, "xxx");
    /* return value is still what WOULD have been written */
    ASSERT_EQ(ret, 5);
}

TEST(snprintf_size_one)
{
    char buf[4];
    int ret;

    strcpy(buf, "xxx");
    ret = snprintf(buf, 1, "hello");
    /* should write only the null terminator */
    ASSERT_EQ(buf[0], '\0');
    ASSERT_EQ(ret, 5);
}

/* ===== snprintf mixed format tests ===== */

TEST(snprintf_mixed)
{
    char buf[128];

    snprintf(buf, sizeof(buf), "%s=%d", "x", 42);
    ASSERT_STR_EQ(buf, "x=42");
}

TEST(snprintf_multiple_args)
{
    char buf[128];

    snprintf(buf, sizeof(buf), "%d+%d=%d", 1, 2, 3);
    ASSERT_STR_EQ(buf, "1+2=3");
}

TEST(snprintf_complex_format)
{
    char buf[128];

    snprintf(buf, sizeof(buf), "[%05d|%s|%x]", 7, "ab", 255);
    ASSERT_STR_EQ(buf, "[00007|ab|ff]");
}

TEST(snprintf_no_format)
{
    char buf[64];
    int ret;

    ret = snprintf(buf, sizeof(buf), "plain text");
    ASSERT_STR_EQ(buf, "plain text");
    ASSERT_EQ(ret, 10);
}

TEST(snprintf_empty_format)
{
    char buf[64];
    int ret;

    buf[0] = 'X';
    ret = snprintf(buf, sizeof(buf), "%s", "");
    ASSERT_STR_EQ(buf, "");
    ASSERT_EQ(ret, 0);
}

/* ===== snprintf %ld (long) tests ===== */

TEST(snprintf_ld_basic)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%ld", 123456789L);
    ASSERT_STR_EQ(buf, "123456789");
}

TEST(snprintf_ld_negative)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%ld", -123456789L);
    ASSERT_STR_EQ(buf, "-123456789");
}

/* ===== snprintf %u (unsigned) tests ===== */

TEST(snprintf_u_basic)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%u", 42u);
    ASSERT_STR_EQ(buf, "42");
}

TEST(snprintf_u_zero)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%u", 0u);
    ASSERT_STR_EQ(buf, "0");
}

/* ===== sprintf tests ===== */

TEST(sprintf_basic)
{
    char buf[64];
    int ret;

    ret = sprintf(buf, "hello %s", "world");
    ASSERT_STR_EQ(buf, "hello world");
    ASSERT_EQ(ret, 11);
}

TEST(sprintf_d)
{
    char buf[64];

    sprintf(buf, "%d", 42);
    ASSERT_STR_EQ(buf, "42");
}

TEST(sprintf_mixed)
{
    char buf[128];

    sprintf(buf, "%s=%d (0x%x)", "val", 255, 255);
    ASSERT_STR_EQ(buf, "val=255 (0xff)");
}

TEST(sprintf_empty)
{
    char buf[64];
    int ret;

    ret = sprintf(buf, "%s", "");
    ASSERT_STR_EQ(buf, "");
    ASSERT_EQ(ret, 0);
}

int main(void)
{
    printf("test_stdio:\n");

    /* snprintf %d */
    RUN_TEST(snprintf_d_positive);
    RUN_TEST(snprintf_d_negative);
    RUN_TEST(snprintf_d_zero);
    RUN_TEST(snprintf_d_large);
    RUN_TEST(snprintf_d_negative_large);

    /* snprintf %s */
    RUN_TEST(snprintf_s_basic);
    RUN_TEST(snprintf_s_empty);
    RUN_TEST(snprintf_s_with_text);

    /* snprintf %x */
    RUN_TEST(snprintf_x_basic);
    RUN_TEST(snprintf_x_zero);
    RUN_TEST(snprintf_x_large);
    RUN_TEST(snprintf_x_one);

    /* snprintf %c */
    RUN_TEST(snprintf_c_basic);
    RUN_TEST(snprintf_c_space);
    RUN_TEST(snprintf_c_in_string);

    /* snprintf %p */
    RUN_TEST(snprintf_p_nonnull);
    RUN_TEST(snprintf_p_null);

    /* snprintf %% */
    RUN_TEST(snprintf_percent_literal);
    RUN_TEST(snprintf_percent_in_string);
    RUN_TEST(snprintf_percent_multiple);

    /* snprintf field widths */
    RUN_TEST(snprintf_width_d);
    RUN_TEST(snprintf_width_d_negative);
    RUN_TEST(snprintf_width_s);
    RUN_TEST(snprintf_width_smaller_than_value);

    /* snprintf zero padding */
    RUN_TEST(snprintf_zero_pad_d);
    RUN_TEST(snprintf_zero_pad_d_negative);
    RUN_TEST(snprintf_zero_pad_x);
    RUN_TEST(snprintf_zero_pad_width_1);

    /* snprintf truncation */
    RUN_TEST(snprintf_truncation);
    RUN_TEST(snprintf_exact_fit);
    RUN_TEST(snprintf_size_zero);
    RUN_TEST(snprintf_size_one);

    /* snprintf mixed */
    RUN_TEST(snprintf_mixed);
    RUN_TEST(snprintf_multiple_args);
    RUN_TEST(snprintf_complex_format);
    RUN_TEST(snprintf_no_format);
    RUN_TEST(snprintf_empty_format);

    /* snprintf %ld */
    RUN_TEST(snprintf_ld_basic);
    RUN_TEST(snprintf_ld_negative);

    /* snprintf %u */
    RUN_TEST(snprintf_u_basic);
    RUN_TEST(snprintf_u_zero);

    /* sprintf */
    RUN_TEST(sprintf_basic);
    RUN_TEST(sprintf_d);
    RUN_TEST(sprintf_mixed);
    RUN_TEST(sprintf_empty);

    TEST_SUMMARY();
    return tests_failed;
}
