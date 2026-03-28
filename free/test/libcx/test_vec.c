/*
 * test_vec.c - Tests for cx_vec dynamic array macros.
 * Part of libcx tests. Pure C89.
 */

#include "test.h"
#include "cx_vec.h"

CX_VEC_DEFINE(int_vec, int)
CX_VEC_DEFINE(ptr_vec, void *)

TEST(vec_new)
{
    int_vec v = int_vec_new();
    ASSERT_EQ(int_vec_len(&v), 0);
    ASSERT_EQ(int_vec_cap(&v), 0);
    ASSERT_NULL(v.data);
    int_vec_free(&v);
}

TEST(vec_push_get)
{
    int_vec v = int_vec_new();
    int_vec_push(&v, 10);
    int_vec_push(&v, 20);
    int_vec_push(&v, 30);
    ASSERT_EQ(int_vec_len(&v), 3);
    ASSERT_EQ(int_vec_get(&v, 0), 10);
    ASSERT_EQ(int_vec_get(&v, 1), 20);
    ASSERT_EQ(int_vec_get(&v, 2), 30);
    int_vec_free(&v);
}

TEST(vec_pop)
{
    int_vec v = int_vec_new();
    int val;
    int_vec_push(&v, 1);
    int_vec_push(&v, 2);
    int_vec_push(&v, 3);
    val = int_vec_pop(&v);
    ASSERT_EQ(val, 3);
    ASSERT_EQ(int_vec_len(&v), 2);
    val = int_vec_pop(&v);
    ASSERT_EQ(val, 2);
    int_vec_free(&v);
}

TEST(vec_set)
{
    int_vec v = int_vec_new();
    int_vec_push(&v, 0);
    int_vec_push(&v, 0);
    int_vec_set(&v, 0, 42);
    int_vec_set(&v, 1, 99);
    ASSERT_EQ(int_vec_get(&v, 0), 42);
    ASSERT_EQ(int_vec_get(&v, 1), 99);
    int_vec_free(&v);
}

TEST(vec_grow)
{
    int_vec v = int_vec_new();
    int i;
    for (i = 0; i < 1000; i++) {
        int_vec_push(&v, i);
    }
    ASSERT_EQ(int_vec_len(&v), 1000);
    ASSERT_GE(int_vec_cap(&v), 1000);
    for (i = 0; i < 1000; i++) {
        ASSERT_EQ(int_vec_get(&v, i), i);
    }
    int_vec_free(&v);
}

TEST(vec_ptr)
{
    ptr_vec v = ptr_vec_new();
    int a = 1, b = 2, c = 3;
    void *popped;
    ptr_vec_push(&v, &a);
    ptr_vec_push(&v, &b);
    ptr_vec_push(&v, &c);
    ASSERT_EQ(ptr_vec_len(&v), 3);
    ASSERT_EQ(ptr_vec_cap(&v), 8);
    ASSERT_EQ(*(int *)ptr_vec_get(&v, 0), 1);
    ASSERT_EQ(*(int *)ptr_vec_get(&v, 1), 2);
    ptr_vec_set(&v, 1, &c);
    ASSERT_EQ(*(int *)ptr_vec_get(&v, 1), 3);
    popped = ptr_vec_pop(&v);
    ASSERT_EQ(*(int *)popped, 3);
    ASSERT_EQ(ptr_vec_len(&v), 2);
    ptr_vec_free(&v);
}

TEST(vec_free)
{
    int_vec v = int_vec_new();
    int_vec_push(&v, 42);
    int_vec_free(&v);
    ASSERT_EQ(int_vec_len(&v), 0);
    ASSERT_EQ(int_vec_cap(&v), 0);
    ASSERT_NULL(v.data);
}

int main(void)
{
    printf("test_vec:\n");
    RUN_TEST(vec_new);
    RUN_TEST(vec_push_get);
    RUN_TEST(vec_pop);
    RUN_TEST(vec_set);
    RUN_TEST(vec_grow);
    RUN_TEST(vec_ptr);
    RUN_TEST(vec_free);
    TEST_SUMMARY();
    return tests_failed ? 1 : 0;
}
