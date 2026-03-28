/*
 * test_map.c - Tests for cx_map hash map.
 * Part of libcx tests. Pure C89.
 */

#include "test.h"
#include "cx_map.h"

TEST(map_create)
{
    cx_map *m = cx_map_create();
    ASSERT_NOT_NULL(m);
    ASSERT_EQ(cx_map_count(m), 0);
    cx_map_free(m);
}

TEST(map_set_get)
{
    cx_map *m = cx_map_create();
    int a = 10, b = 20, c = 30;
    cx_map_set(m, "alpha", &a);
    cx_map_set(m, "beta", &b);
    cx_map_set(m, "gamma", &c);

    ASSERT_EQ(cx_map_count(m), 3);
    ASSERT_EQ(*(int *)cx_map_get(m, "alpha"), 10);
    ASSERT_EQ(*(int *)cx_map_get(m, "beta"), 20);
    ASSERT_EQ(*(int *)cx_map_get(m, "gamma"), 30);
    cx_map_free(m);
}

TEST(map_overwrite)
{
    cx_map *m = cx_map_create();
    int a = 10, b = 99;
    cx_map_set(m, "key", &a);
    ASSERT_EQ(*(int *)cx_map_get(m, "key"), 10);
    cx_map_set(m, "key", &b);
    ASSERT_EQ(*(int *)cx_map_get(m, "key"), 99);
    ASSERT_EQ(cx_map_count(m), 1);
    cx_map_free(m);
}

TEST(map_get_missing)
{
    cx_map *m = cx_map_create();
    ASSERT_NULL(cx_map_get(m, "nope"));
    cx_map_free(m);
}

TEST(map_del)
{
    cx_map *m = cx_map_create();
    int a = 10, b = 20;
    cx_map_set(m, "x", &a);
    cx_map_set(m, "y", &b);
    ASSERT_EQ(cx_map_count(m), 2);

    ASSERT_EQ(cx_map_del(m, "x"), 1);
    ASSERT_EQ(cx_map_count(m), 1);
    ASSERT_NULL(cx_map_get(m, "x"));
    ASSERT_EQ(*(int *)cx_map_get(m, "y"), 20);

    ASSERT_EQ(cx_map_del(m, "x"), 0); /* already deleted */
    cx_map_free(m);
}

TEST(map_many)
{
    cx_map *m = cx_map_create();
    char key[16];
    int values[200];
    int i;

    for (i = 0; i < 200; i++) {
        values[i] = i * 7;
        sprintf(key, "key_%d", i);
        cx_map_set(m, key, &values[i]);
    }
    ASSERT_EQ(cx_map_count(m), 200);

    for (i = 0; i < 200; i++) {
        int *v;
        sprintf(key, "key_%d", i);
        v = (int *)cx_map_get(m, key);
        ASSERT_NOT_NULL(v);
        ASSERT_EQ(*v, i * 7);
    }
    cx_map_free(m);
}

int main(void)
{
    printf("test_map:\n");
    RUN_TEST(map_create);
    RUN_TEST(map_set_get);
    RUN_TEST(map_overwrite);
    RUN_TEST(map_get_missing);
    RUN_TEST(map_del);
    RUN_TEST(map_many);
    TEST_SUMMARY();
    return tests_failed ? 1 : 0;
}
