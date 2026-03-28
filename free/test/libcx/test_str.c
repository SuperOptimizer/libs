/*
 * test_str.c - Tests for cx_str dynamic string builder.
 * Part of libcx tests. Pure C89.
 */

#include "test.h"
#include "cx_str.h"
#include <string.h>

TEST(str_new)
{
    cx_str s = cx_str_new();
    ASSERT_EQ(cx_str_len(&s), 0);
    cx_str_free(&s);
}

TEST(str_appendc)
{
    cx_str s = cx_str_new();
    cx_str_appendc(&s, 'H');
    cx_str_appendc(&s, 'i');
    ASSERT_EQ(cx_str_len(&s), 2);
    ASSERT_STR_EQ(cx_str_cstr(&s), "Hi");
    cx_str_free(&s);
}

TEST(str_append)
{
    cx_str s = cx_str_new();
    cx_str_append(&s, "Hello", 5);
    cx_str_append(&s, ", ", 2);
    cx_str_append(&s, "world!", 6);
    ASSERT_EQ(cx_str_len(&s), 13);
    ASSERT_STR_EQ(cx_str_cstr(&s), "Hello, world!");
    cx_str_free(&s);
}

TEST(str_appendf)
{
    cx_str s = cx_str_new();
    cx_str_appendf(&s, "x=%d", 42);
    ASSERT_STR_EQ(cx_str_cstr(&s), "x=42");
    cx_str_appendf(&s, " y=%s", "hello");
    ASSERT_STR_EQ(cx_str_cstr(&s), "x=42 y=hello");
    cx_str_free(&s);
}

TEST(str_clear)
{
    cx_str s = cx_str_new();
    cx_str_append(&s, "hello", 5);
    cx_str_clear(&s);
    ASSERT_EQ(cx_str_len(&s), 0);
    ASSERT_STR_EQ(cx_str_cstr(&s), "");
    cx_str_free(&s);
}

TEST(str_dup)
{
    cx_str s = cx_str_new();
    cx_str d;
    cx_str_append(&s, "test", 4);
    d = cx_str_dup(&s);
    ASSERT_STR_EQ(cx_str_cstr(&d), "test");
    ASSERT_EQ(cx_str_len(&d), 4);
    /* Modify original, dup should be independent */
    cx_str_appendc(&s, '!');
    ASSERT_STR_EQ(cx_str_cstr(&d), "test");
    cx_str_free(&s);
    cx_str_free(&d);
}

TEST(str_slice)
{
    cx_str s = cx_str_new();
    cx_str sl;
    cx_str_append(&s, "Hello, world!", 13);
    sl = cx_str_slice(&s, 7, 12);
    ASSERT_STR_EQ(cx_str_cstr(&sl), "world");
    ASSERT_EQ(cx_str_len(&sl), 5);
    cx_str_free(&sl);
    cx_str_free(&s);
}

TEST(str_find)
{
    cx_str s = cx_str_new();
    cx_str_append(&s, "Hello, world!", 13);
    ASSERT_EQ(cx_str_find(&s, "world"), 7);
    ASSERT_EQ(cx_str_find(&s, "xyz"), -1);
    ASSERT_EQ(cx_str_find(&s, "Hello"), 0);
    cx_str_free(&s);
}

TEST(str_grow)
{
    cx_str s = cx_str_new();
    int i;
    for (i = 0; i < 1000; i++) {
        cx_str_appendc(&s, 'x');
    }
    ASSERT_EQ(cx_str_len(&s), 1000);
    ASSERT(cx_str_cstr(&s)[999] == 'x');
    ASSERT(cx_str_cstr(&s)[1000] == '\0');
    cx_str_free(&s);
}

int main(void)
{
    printf("test_str:\n");
    RUN_TEST(str_new);
    RUN_TEST(str_appendc);
    RUN_TEST(str_append);
    RUN_TEST(str_appendf);
    RUN_TEST(str_clear);
    RUN_TEST(str_dup);
    RUN_TEST(str_slice);
    RUN_TEST(str_find);
    RUN_TEST(str_grow);
    TEST_SUMMARY();
    return tests_failed ? 1 : 0;
}
