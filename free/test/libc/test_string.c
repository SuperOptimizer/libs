/*
 * test_string.c - Tests for string.h and memory functions.
 * Pure C89. No external dependencies.
 */

#include "../test.h"
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

/* ===== memcpy tests ===== */

TEST(memcpy_basic)
{
    char src[] = "hello";
    char dst[8];
    void *ret;

    memset(dst, 0, sizeof(dst));
    ret = memcpy(dst, src, 6);
    ASSERT_STR_EQ(dst, "hello");
    ASSERT_EQ((long)ret, (long)dst);
}

TEST(memcpy_single_byte)
{
    char src = 'X';
    char dst = 'A';

    memcpy(&dst, &src, 1);
    ASSERT_EQ(dst, 'X');
}

TEST(memcpy_zero_length)
{
    char src[] = "abc";
    char dst[] = "xyz";

    memcpy(dst, src, 0);
    ASSERT_STR_EQ(dst, "xyz");
}

TEST(memcpy_binary_data)
{
    unsigned char src[4];
    unsigned char dst[4];

    src[0] = 0x00;
    src[1] = 0xFF;
    src[2] = 0x7F;
    src[3] = 0x80;
    memcpy(dst, src, 4);
    ASSERT_EQ(dst[0], 0x00);
    ASSERT_EQ(dst[1], 0xFF);
    ASSERT_EQ(dst[2], 0x7F);
    ASSERT_EQ(dst[3], 0x80);
}

/* ===== memmove tests ===== */

TEST(memmove_nonoverlapping)
{
    char src[] = "hello";
    char dst[8];

    memset(dst, 0, sizeof(dst));
    memmove(dst, src, 6);
    ASSERT_STR_EQ(dst, "hello");
}

TEST(memmove_overlap_forward)
{
    char buf[] = "abcdefgh";

    /* src at buf+0, dst at buf+2, copy 4 bytes: overlap */
    memmove(buf + 2, buf, 4);
    ASSERT_EQ(buf[2], 'a');
    ASSERT_EQ(buf[3], 'b');
    ASSERT_EQ(buf[4], 'c');
    ASSERT_EQ(buf[5], 'd');
}

TEST(memmove_overlap_backward)
{
    char buf[] = "abcdefgh";

    /* src at buf+2, dst at buf+0, copy 4 bytes */
    memmove(buf, buf + 2, 4);
    ASSERT_EQ(buf[0], 'c');
    ASSERT_EQ(buf[1], 'd');
    ASSERT_EQ(buf[2], 'e');
    ASSERT_EQ(buf[3], 'f');
}

TEST(memmove_zero_length)
{
    char buf[] = "abc";

    memmove(buf, buf + 1, 0);
    ASSERT_STR_EQ(buf, "abc");
}

TEST(memmove_same_pointer)
{
    char buf[] = "hello";

    memmove(buf, buf, 5);
    ASSERT_STR_EQ(buf, "hello");
}

/* ===== memset tests ===== */

TEST(memset_basic)
{
    char buf[8];
    int i;

    memset(buf, 'A', 4);
    for (i = 0; i < 4; i++) {
        ASSERT_EQ(buf[i], 'A');
    }
}

TEST(memset_zero)
{
    char buf[8];
    int i;

    memset(buf, 'X', 8);
    memset(buf, 0, 8);
    for (i = 0; i < 8; i++) {
        ASSERT_EQ(buf[i], 0);
    }
}

TEST(memset_zero_length)
{
    char buf[] = "abc";
    size_t zero = 0;

    memset(buf, 'X', zero);
    ASSERT_STR_EQ(buf, "abc");
}

TEST(memset_returns_pointer)
{
    char buf[4];
    void *ret;

    ret = memset(buf, 0, 4);
    ASSERT_EQ((long)ret, (long)buf);
}

/* ===== memcmp tests ===== */

TEST(memcmp_equal)
{
    ASSERT_EQ(memcmp("abcd", "abcd", 4), 0);
}

TEST(memcmp_less)
{
    ASSERT(memcmp("abcd", "abce", 4) < 0);
}

TEST(memcmp_greater)
{
    ASSERT(memcmp("abce", "abcd", 4) > 0);
}

TEST(memcmp_zero_length)
{
    ASSERT_EQ(memcmp("abc", "xyz", 0), 0);
}

TEST(memcmp_partial)
{
    /* compare only first 3 bytes */
    ASSERT_EQ(memcmp("abcX", "abcY", 3), 0);
}

TEST(memcmp_binary)
{
    unsigned char a[2];
    unsigned char b[2];

    a[0] = 0x00;
    a[1] = 0xFF;
    b[0] = 0x00;
    b[1] = 0x80;
    ASSERT(memcmp(a, b, 2) > 0);
}

/* ===== memchr tests ===== */

TEST(memchr_found)
{
    const char *s = "hello";
    char *p;

    p = (char *)memchr(s, 'l', 5);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((long)p, (long)(s + 2));
}

TEST(memchr_not_found)
{
    const char *s = "hello";

    ASSERT_NULL(memchr(s, 'z', 5));
}

TEST(memchr_first_byte)
{
    const char *s = "hello";
    char *p;

    p = (char *)memchr(s, 'h', 5);
    ASSERT_EQ((long)p, (long)s);
}

TEST(memchr_last_byte)
{
    const char *s = "hello";
    char *p;

    p = (char *)memchr(s, 'o', 5);
    ASSERT_EQ((long)p, (long)(s + 4));
}

TEST(memchr_zero_length)
{
    ASSERT_NULL(memchr("hello", 'h', 0));
}

TEST(memchr_null_byte)
{
    const char *s = "ab\0cd";
    char *p;

    p = (char *)memchr(s, '\0', 5);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((long)p, (long)(s + 2));
}

/* ===== strlen tests ===== */

TEST(strlen_basic)
{
    ASSERT_EQ(strlen("hello"), 5);
}

TEST(strlen_empty)
{
    ASSERT_EQ(strlen(""), 0);
}

TEST(strlen_single)
{
    ASSERT_EQ(strlen("x"), 1);
}

TEST(strlen_with_spaces)
{
    ASSERT_EQ(strlen("hello world"), 11);
}

/* ===== strcmp tests ===== */

TEST(strcmp_equal)
{
    ASSERT_EQ(strcmp("hello", "hello"), 0);
}

TEST(strcmp_less)
{
    ASSERT(strcmp("abc", "abd") < 0);
}

TEST(strcmp_greater)
{
    ASSERT(strcmp("abd", "abc") > 0);
}

TEST(strcmp_prefix)
{
    /* "abc" < "abcd" because '\0' < 'd' */
    ASSERT(strcmp("abc", "abcd") < 0);
}

TEST(strcmp_empty)
{
    ASSERT_EQ(strcmp("", ""), 0);
}

TEST(strcmp_empty_vs_nonempty)
{
    ASSERT(strcmp("", "a") < 0);
    ASSERT(strcmp("a", "") > 0);
}

/* ===== strncmp tests ===== */

TEST(strncmp_equal)
{
    ASSERT_EQ(strncmp("hello", "hello", 5), 0);
}

TEST(strncmp_partial_match)
{
    ASSERT_EQ(strncmp("helloX", "helloY", 5), 0);
}

TEST(strncmp_differ)
{
    ASSERT(strncmp("abc", "abd", 3) < 0);
}

TEST(strncmp_zero_length)
{
    ASSERT_EQ(strncmp("abc", "xyz", 0), 0);
}

TEST(strncmp_shorter_string)
{
    /* comparing "ab" with "abc" up to n=3 */
    ASSERT(strncmp("ab", "abc", 3) < 0);
}

/* ===== strcpy tests ===== */

TEST(strcpy_basic)
{
    char dst[16];

    strcpy(dst, "hello");
    ASSERT_STR_EQ(dst, "hello");
}

TEST(strcpy_empty)
{
    char dst[4];

    strcpy(dst, "");
    ASSERT_STR_EQ(dst, "");
    ASSERT_EQ(dst[0], '\0');
}

TEST(strcpy_returns_dst)
{
    char dst[16];
    char *ret;

    ret = strcpy(dst, "test");
    ASSERT_EQ((long)ret, (long)dst);
}

TEST(strcpy_overwrites)
{
    char dst[16];

    strcpy(dst, "hello world!");
    strcpy(dst, "hi");
    ASSERT_STR_EQ(dst, "hi");
}

/* ===== strncpy tests ===== */

TEST(strncpy_basic)
{
    char dst[16];

    memset(dst, 'X', sizeof(dst));
    strncpy(dst, "hello", 16);
    ASSERT_STR_EQ(dst, "hello");
}

TEST(strncpy_truncate)
{
    char dst[4];
    char src[] = "hello";

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    strncpy(dst, src, 3);
#pragma GCC diagnostic pop
    /* strncpy does not null-terminate when truncated */
    ASSERT_EQ(dst[0], 'h');
    ASSERT_EQ(dst[1], 'e');
    ASSERT_EQ(dst[2], 'l');
}

TEST(strncpy_pad_with_nulls)
{
    char dst[8];

    memset(dst, 'X', sizeof(dst));
    strncpy(dst, "hi", 8);
    ASSERT_STR_EQ(dst, "hi");
    /* remaining bytes should be zeroed */
    ASSERT_EQ(dst[2], '\0');
    ASSERT_EQ(dst[3], '\0');
    ASSERT_EQ(dst[7], '\0');
}

TEST(strncpy_exact_fit)
{
    char dst[5];
    char src[] = "hello";

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    strncpy(dst, src, 5);
#pragma GCC diagnostic pop
    ASSERT_EQ(dst[0], 'h');
    ASSERT_EQ(dst[4], 'o');
}

/* ===== strcat tests ===== */

TEST(strcat_basic)
{
    char buf[32];

    strcpy(buf, "hello");
    strcat(buf, " world");
    ASSERT_STR_EQ(buf, "hello world");
}

TEST(strcat_empty_src)
{
    char buf[16];

    strcpy(buf, "hello");
    strcat(buf, "");
    ASSERT_STR_EQ(buf, "hello");
}

TEST(strcat_empty_dst)
{
    char buf[16];

    buf[0] = '\0';
    strcat(buf, "hello");
    ASSERT_STR_EQ(buf, "hello");
}

TEST(strcat_returns_dst)
{
    char buf[32];
    char *ret;

    strcpy(buf, "a");
    ret = strcat(buf, "b");
    ASSERT_EQ((long)ret, (long)buf);
}

TEST(strcat_multiple)
{
    char buf[32];

    strcpy(buf, "");
    strcat(buf, "a");
    strcat(buf, "b");
    strcat(buf, "c");
    ASSERT_STR_EQ(buf, "abc");
}

/* ===== strchr tests ===== */

TEST(strchr_found)
{
    const char *s = "hello";
    char *p;

    p = strchr(s, 'l');
    ASSERT_NOT_NULL(p);
    /* first 'l' is at index 2 */
    ASSERT_EQ((long)p, (long)(s + 2));
}

TEST(strchr_not_found)
{
    ASSERT_NULL(strchr("hello", 'z'));
}

TEST(strchr_first_char)
{
    const char *s = "hello";
    char *p;

    p = strchr(s, 'h');
    ASSERT_EQ((long)p, (long)s);
}

TEST(strchr_null_terminator)
{
    const char *s = "hello";
    char *p;

    p = strchr(s, '\0');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(*p, '\0');
    ASSERT_EQ((long)p, (long)(s + 5));
}

TEST(strchr_empty_string)
{
    const char *s = "";
    char *p;

    p = strchr(s, 'a');
    ASSERT_NULL(p);
}

/* ===== strrchr tests ===== */

TEST(strrchr_found)
{
    const char *s = "hello";
    char *p;

    p = strrchr(s, 'l');
    ASSERT_NOT_NULL(p);
    /* last 'l' is at index 3 */
    ASSERT_EQ((long)p, (long)(s + 3));
}

TEST(strrchr_not_found)
{
    ASSERT_NULL(strrchr("hello", 'z'));
}

TEST(strrchr_single_occurrence)
{
    const char *s = "hello";
    char *p;

    p = strrchr(s, 'h');
    ASSERT_EQ((long)p, (long)s);
}

TEST(strrchr_null_terminator)
{
    const char *s = "hello";
    char *p;

    p = strrchr(s, '\0');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(*p, '\0');
    ASSERT_EQ((long)p, (long)(s + 5));
}

TEST(strrchr_multiple)
{
    const char *s = "abcabc";
    char *p;

    p = strrchr(s, 'a');
    ASSERT_EQ((long)p, (long)(s + 3));
}

/* ===== strstr tests ===== */

TEST(strstr_found)
{
    const char *s = "hello world";
    char *p;

    p = strstr(s, "world");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((long)p, (long)(s + 6));
}

TEST(strstr_not_found)
{
    ASSERT_NULL(strstr("hello", "xyz"));
}

TEST(strstr_empty_needle)
{
    const char *s = "hello";
    char *p;

    p = strstr(s, "");
    ASSERT_EQ((long)p, (long)s);
}

TEST(strstr_at_beginning)
{
    const char *s = "hello";
    char *p;

    p = strstr(s, "hel");
    ASSERT_EQ((long)p, (long)s);
}

TEST(strstr_at_end)
{
    const char *s = "hello";
    char *p;

    p = strstr(s, "llo");
    ASSERT_EQ((long)p, (long)(s + 2));
}

TEST(strstr_full_match)
{
    const char *s = "hello";
    char *p;

    p = strstr(s, "hello");
    ASSERT_EQ((long)p, (long)s);
}

TEST(strstr_partial_overlap)
{
    /* "aab" in "aaab" - the first "aa" is not followed by 'b' */
    char *p;

    p = strstr("aaab", "aab");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p, "aab");
}

TEST(strstr_needle_longer)
{
    ASSERT_NULL(strstr("hi", "hello"));
}

int main(void)
{
    printf("test_string:\n");

    /* memcpy */
    RUN_TEST(memcpy_basic);
    RUN_TEST(memcpy_single_byte);
    RUN_TEST(memcpy_zero_length);
    RUN_TEST(memcpy_binary_data);

    /* memmove */
    RUN_TEST(memmove_nonoverlapping);
    RUN_TEST(memmove_overlap_forward);
    RUN_TEST(memmove_overlap_backward);
    RUN_TEST(memmove_zero_length);
    RUN_TEST(memmove_same_pointer);

    /* memset */
    RUN_TEST(memset_basic);
    RUN_TEST(memset_zero);
    RUN_TEST(memset_zero_length);
    RUN_TEST(memset_returns_pointer);

    /* memcmp */
    RUN_TEST(memcmp_equal);
    RUN_TEST(memcmp_less);
    RUN_TEST(memcmp_greater);
    RUN_TEST(memcmp_zero_length);
    RUN_TEST(memcmp_partial);
    RUN_TEST(memcmp_binary);

    /* memchr */
    RUN_TEST(memchr_found);
    RUN_TEST(memchr_not_found);
    RUN_TEST(memchr_first_byte);
    RUN_TEST(memchr_last_byte);
    RUN_TEST(memchr_zero_length);
    RUN_TEST(memchr_null_byte);

    /* strlen */
    RUN_TEST(strlen_basic);
    RUN_TEST(strlen_empty);
    RUN_TEST(strlen_single);
    RUN_TEST(strlen_with_spaces);

    /* strcmp */
    RUN_TEST(strcmp_equal);
    RUN_TEST(strcmp_less);
    RUN_TEST(strcmp_greater);
    RUN_TEST(strcmp_prefix);
    RUN_TEST(strcmp_empty);
    RUN_TEST(strcmp_empty_vs_nonempty);

    /* strncmp */
    RUN_TEST(strncmp_equal);
    RUN_TEST(strncmp_partial_match);
    RUN_TEST(strncmp_differ);
    RUN_TEST(strncmp_zero_length);
    RUN_TEST(strncmp_shorter_string);

    /* strcpy */
    RUN_TEST(strcpy_basic);
    RUN_TEST(strcpy_empty);
    RUN_TEST(strcpy_returns_dst);
    RUN_TEST(strcpy_overwrites);

    /* strncpy */
    RUN_TEST(strncpy_basic);
    RUN_TEST(strncpy_truncate);
    RUN_TEST(strncpy_pad_with_nulls);
    RUN_TEST(strncpy_exact_fit);

    /* strcat */
    RUN_TEST(strcat_basic);
    RUN_TEST(strcat_empty_src);
    RUN_TEST(strcat_empty_dst);
    RUN_TEST(strcat_returns_dst);
    RUN_TEST(strcat_multiple);

    /* strchr */
    RUN_TEST(strchr_found);
    RUN_TEST(strchr_not_found);
    RUN_TEST(strchr_first_char);
    RUN_TEST(strchr_null_terminator);
    RUN_TEST(strchr_empty_string);

    /* strrchr */
    RUN_TEST(strrchr_found);
    RUN_TEST(strrchr_not_found);
    RUN_TEST(strrchr_single_occurrence);
    RUN_TEST(strrchr_null_terminator);
    RUN_TEST(strrchr_multiple);

    /* strstr */
    RUN_TEST(strstr_found);
    RUN_TEST(strstr_not_found);
    RUN_TEST(strstr_empty_needle);
    RUN_TEST(strstr_at_beginning);
    RUN_TEST(strstr_at_end);
    RUN_TEST(strstr_full_match);
    RUN_TEST(strstr_partial_overlap);
    RUN_TEST(strstr_needle_longer);

    TEST_SUMMARY();
    return tests_failed;
}
