/*
 * test_stdlib.c - Tests for stdlib.h functions.
 * Pure C89. No external dependencies.
 */

#include "../test.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* ===== atoi tests ===== */

TEST(atoi_positive)
{
    ASSERT_EQ(atoi("42"), 42);
    ASSERT_EQ(atoi("0"), 0);
    ASSERT_EQ(atoi("12345"), 12345);
}

TEST(atoi_negative)
{
    ASSERT_EQ(atoi("-1"), -1);
    ASSERT_EQ(atoi("-42"), -42);
    ASSERT_EQ(atoi("-99999"), -99999);
}

TEST(atoi_leading_whitespace)
{
    ASSERT_EQ(atoi("  42"), 42);
    ASSERT_EQ(atoi("\t\n 7"), 7);
}

TEST(atoi_leading_plus)
{
    ASSERT_EQ(atoi("+42"), 42);
}

TEST(atoi_trailing_non_digit)
{
    ASSERT_EQ(atoi("123abc"), 123);
    ASSERT_EQ(atoi("42 "), 42);
}

TEST(atoi_empty_and_nonnumeric)
{
    ASSERT_EQ(atoi(""), 0);
    ASSERT_EQ(atoi("abc"), 0);
}

TEST(atoi_zero_variants)
{
    ASSERT_EQ(atoi("0"), 0);
    ASSERT_EQ(atoi("+0"), 0);
    ASSERT_EQ(atoi("-0"), 0);
    ASSERT_EQ(atoi("00"), 0);
}

/* ===== atol tests ===== */

TEST(atol_positive)
{
    ASSERT_EQ(atol("42"), 42L);
    ASSERT_EQ(atol("100000"), 100000L);
}

TEST(atol_negative)
{
    ASSERT_EQ(atol("-1"), -1L);
    ASSERT_EQ(atol("-100000"), -100000L);
}

TEST(atol_leading_whitespace)
{
    ASSERT_EQ(atol("  42"), 42L);
}

TEST(atol_zero)
{
    ASSERT_EQ(atol("0"), 0L);
}

/* ===== strtol tests ===== */

TEST(strtol_decimal)
{
    char *end;
    long val;

    val = strtol("42", &end, 10);
    ASSERT_EQ(val, 42);
    ASSERT_EQ(*end, '\0');
}

TEST(strtol_negative)
{
    char *end;
    long val;

    val = strtol("-99", &end, 10);
    ASSERT_EQ(val, -99);
    ASSERT_EQ(*end, '\0');
}

TEST(strtol_hex)
{
    char *end;
    long val;

    val = strtol("0xFF", &end, 16);
    ASSERT_EQ(val, 255);

    val = strtol("ff", &end, 16);
    ASSERT_EQ(val, 255);

    val = strtol("0x1A", &end, 16);
    ASSERT_EQ(val, 26);
}

TEST(strtol_octal)
{
    char *end;
    long val;

    val = strtol("077", &end, 8);
    ASSERT_EQ(val, 63);

    val = strtol("10", &end, 8);
    ASSERT_EQ(val, 8);
}

TEST(strtol_base_zero_decimal)
{
    char *end;
    long val;

    val = strtol("42", &end, 0);
    ASSERT_EQ(val, 42);
}

TEST(strtol_base_zero_hex)
{
    char *end;
    long val;

    val = strtol("0x1F", &end, 0);
    ASSERT_EQ(val, 31);
}

TEST(strtol_base_zero_octal)
{
    char *end;
    long val;

    val = strtol("010", &end, 0);
    ASSERT_EQ(val, 8);
}

TEST(strtol_leading_whitespace)
{
    char *end;
    long val;

    val = strtol("  42", &end, 10);
    ASSERT_EQ(val, 42);
}

TEST(strtol_trailing_chars)
{
    char *end;
    long val;

    val = strtol("123abc", &end, 10);
    ASSERT_EQ(val, 123);
    ASSERT_EQ(*end, 'a');
}

TEST(strtol_null_endptr)
{
    long val;

    val = strtol("42", NULL, 10);
    ASSERT_EQ(val, 42);
}

TEST(strtol_no_digits)
{
    char *end;
    long val;
    const char *s = "abc";

    val = strtol(s, &end, 10);
    ASSERT_EQ(val, 0);
    ASSERT_EQ((long)end, (long)s);
}

TEST(strtol_plus_sign)
{
    char *end;
    long val;

    val = strtol("+42", &end, 10);
    ASSERT_EQ(val, 42);
}

TEST(strtol_zero)
{
    char *end;
    long val;

    val = strtol("0", &end, 10);
    ASSERT_EQ(val, 0);
    ASSERT_EQ(*end, '\0');
}

TEST(strtol_binary)
{
    char *end;
    long val;

    val = strtol("1010", &end, 2);
    ASSERT_EQ(val, 10);
}

TEST(strtol_base36)
{
    char *end;
    long val;

    val = strtol("z", &end, 36);
    ASSERT_EQ(val, 35);

    val = strtol("10", &end, 36);
    ASSERT_EQ(val, 36);
}

/* ===== malloc/free tests ===== */

TEST(malloc_basic)
{
    void *p;

    p = malloc(64);
    ASSERT_NOT_NULL(p);
    free(p);
}

TEST(malloc_zero)
{
    void *p;

    /* malloc(0) may return NULL or a valid pointer; either is ok */
    p = malloc(0);
    /* just free whatever we got */
    free(p);
}

TEST(malloc_write_read)
{
    char *p;

    p = (char *)malloc(16);
    ASSERT_NOT_NULL(p);
    strcpy(p, "hello");
    ASSERT_STR_EQ(p, "hello");
    free(p);
}

TEST(malloc_multiple)
{
    void *a;
    void *b;
    void *c;

    a = malloc(32);
    b = malloc(32);
    c = malloc(32);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_NOT_NULL(c);
    /* pointers should be distinct */
    ASSERT(a != b);
    ASSERT(b != c);
    ASSERT(a != c);
    free(a);
    free(b);
    free(c);
}

TEST(malloc_large)
{
    void *p;

    p = malloc(1024 * 1024);  /* 1 MB */
    ASSERT_NOT_NULL(p);
    free(p);
}

/* ===== calloc tests ===== */

TEST(calloc_basic)
{
    int *p;
    int i;

    p = (int *)calloc(10, sizeof(int));
    ASSERT_NOT_NULL(p);
    for (i = 0; i < 10; i++) {
        ASSERT_EQ(p[i], 0);
    }
    free(p);
}

TEST(calloc_zeroed)
{
    unsigned char *p;
    int i;

    p = (unsigned char *)calloc(256, 1);
    ASSERT_NOT_NULL(p);
    for (i = 0; i < 256; i++) {
        ASSERT_EQ(p[i], 0);
    }
    free(p);
}

TEST(calloc_single)
{
    int *p;

    p = (int *)calloc(1, sizeof(int));
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(*p, 0);
    free(p);
}

TEST(calloc_write_read)
{
    char *p;

    p = (char *)calloc(16, 1);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p[0], 0);
    strcpy(p, "test");
    ASSERT_STR_EQ(p, "test");
    free(p);
}

/* ===== realloc tests ===== */

TEST(realloc_grow)
{
    char *p;

    p = (char *)malloc(8);
    ASSERT_NOT_NULL(p);
    strcpy(p, "hello");
    p = (char *)realloc(p, 32);
    ASSERT_NOT_NULL(p);
    /* old content should be preserved */
    ASSERT_STR_EQ(p, "hello");
    free(p);
}

TEST(realloc_shrink)
{
    char *p;

    p = (char *)malloc(64);
    ASSERT_NOT_NULL(p);
    strcpy(p, "hi");
    p = (char *)realloc(p, 8);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p, "hi");
    free(p);
}

TEST(realloc_null_is_malloc)
{
    char *p;

    /* realloc(NULL, size) should behave like malloc(size) */
    p = (char *)realloc(NULL, 32);
    ASSERT_NOT_NULL(p);
    strcpy(p, "test");
    ASSERT_STR_EQ(p, "test");
    free(p);
}

TEST(realloc_zero_is_free)
{
    void *p;

    p = malloc(32);
    ASSERT_NOT_NULL(p);
    /* realloc(p, 0) is implementation-defined but should not crash */
    p = realloc(p, 0);
    /* result may be NULL or valid pointer */
    free(p);
}

TEST(realloc_preserves_data)
{
    int *p;
    int i;

    p = (int *)malloc(10 * sizeof(int));
    ASSERT_NOT_NULL(p);
    for (i = 0; i < 10; i++) {
        p[i] = i * 100;
    }
    p = (int *)realloc(p, 20 * sizeof(int));
    ASSERT_NOT_NULL(p);
    for (i = 0; i < 10; i++) {
        ASSERT_EQ(p[i], i * 100);
    }
    free(p);
}

int main(void)
{
    printf("test_stdlib:\n");

    /* atoi */
    RUN_TEST(atoi_positive);
    RUN_TEST(atoi_negative);
    RUN_TEST(atoi_leading_whitespace);
    RUN_TEST(atoi_leading_plus);
    RUN_TEST(atoi_trailing_non_digit);
    RUN_TEST(atoi_empty_and_nonnumeric);
    RUN_TEST(atoi_zero_variants);

    /* atol */
    RUN_TEST(atol_positive);
    RUN_TEST(atol_negative);
    RUN_TEST(atol_leading_whitespace);
    RUN_TEST(atol_zero);

    /* strtol */
    RUN_TEST(strtol_decimal);
    RUN_TEST(strtol_negative);
    RUN_TEST(strtol_hex);
    RUN_TEST(strtol_octal);
    RUN_TEST(strtol_base_zero_decimal);
    RUN_TEST(strtol_base_zero_hex);
    RUN_TEST(strtol_base_zero_octal);
    RUN_TEST(strtol_leading_whitespace);
    RUN_TEST(strtol_trailing_chars);
    RUN_TEST(strtol_null_endptr);
    RUN_TEST(strtol_no_digits);
    RUN_TEST(strtol_plus_sign);
    RUN_TEST(strtol_zero);
    RUN_TEST(strtol_binary);
    RUN_TEST(strtol_base36);

    /* malloc/free */
    RUN_TEST(malloc_basic);
    RUN_TEST(malloc_zero);
    RUN_TEST(malloc_write_read);
    RUN_TEST(malloc_multiple);
    RUN_TEST(malloc_large);

    /* calloc */
    RUN_TEST(calloc_basic);
    RUN_TEST(calloc_zeroed);
    RUN_TEST(calloc_single);
    RUN_TEST(calloc_write_read);

    /* realloc */
    RUN_TEST(realloc_grow);
    RUN_TEST(realloc_shrink);
    RUN_TEST(realloc_null_is_malloc);
    RUN_TEST(realloc_zero_is_free);
    RUN_TEST(realloc_preserves_data);

    TEST_SUMMARY();
    return tests_failed;
}
