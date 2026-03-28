/*
 * test_dirent.c - Tests for dirent.h functions.
 * Pure C89. No external dependencies.
 */

#include "../test.h"
#include <dirent.h>
#include <string.h>

/* ===== opendir/closedir tests ===== */

TEST(opendir_root)
{
    DIR *d;

    d = opendir("/");
    ASSERT_NOT_NULL(d);
    closedir(d);
}

TEST(opendir_nonexistent)
{
    DIR *d;

    d = opendir("/nonexistent_path_xyz");
    ASSERT_NULL(d);
}

TEST(closedir_success)
{
    DIR *d;
    int ret;

    d = opendir("/");
    ASSERT_NOT_NULL(d);
    ret = closedir(d);
    ASSERT_EQ(ret, 0);
}

/* ===== readdir tests ===== */

TEST(readdir_root_has_entries)
{
    DIR *d;
    struct dirent *ent;
    int count;

    d = opendir("/");
    ASSERT_NOT_NULL(d);

    count = 0;
    while ((ent = readdir(d)) != NULL) {
        count++;
        /* every entry should have a non-empty name */
        ASSERT(strlen(ent->d_name) > 0);
    }
    ASSERT(count > 0);
    closedir(d);
}

TEST(readdir_has_dot_entries)
{
    DIR *d;
    struct dirent *ent;
    int found_dot;
    int found_dotdot;

    d = opendir("/tmp");
    ASSERT_NOT_NULL(d);

    found_dot = 0;
    found_dotdot = 0;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0) {
            found_dot = 1;
        }
        if (strcmp(ent->d_name, "..") == 0) {
            found_dotdot = 1;
        }
    }
    ASSERT(found_dot);
    ASSERT(found_dotdot);
    closedir(d);
}

TEST(readdir_returns_null_at_end)
{
    DIR *d;
    struct dirent *ent;

    d = opendir("/");
    ASSERT_NOT_NULL(d);

    /* consume all entries */
    while ((ent = readdir(d)) != NULL) {
        /* keep reading */
    }
    /* next call should also return NULL */
    ent = readdir(d);
    ASSERT_NULL(ent);
    closedir(d);
}

int main(void)
{
    printf("test_dirent:\n");

    /* opendir/closedir */
    RUN_TEST(opendir_root);
    RUN_TEST(opendir_nonexistent);
    RUN_TEST(closedir_success);

    /* readdir */
    RUN_TEST(readdir_root_has_entries);
    RUN_TEST(readdir_has_dot_entries);
    RUN_TEST(readdir_returns_null_at_end);

    TEST_SUMMARY();
    return tests_failed;
}
