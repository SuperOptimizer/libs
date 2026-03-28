/*
 * test_mman.c - Tests for sys/mman.h functions.
 * Pure C89. No external dependencies.
 */

#include "../test.h"
#include <sys/mman.h>
#include <string.h>

/* ===== mmap tests ===== */

TEST(mmap_anonymous)
{
    void *p;

    p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT(p != MAP_FAILED);

    /* should be writable */
    memset(p, 0xAB, 4096);
    ASSERT_EQ(((unsigned char *)p)[0], 0xAB);
    ASSERT_EQ(((unsigned char *)p)[4095], 0xAB);

    munmap(p, 4096);
}

TEST(mmap_large)
{
    void *p;
    size_t sz;

    sz = 64 * 1024; /* 64K */
    p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT(p != MAP_FAILED);

    memset(p, 0, sz);
    munmap(p, sz);
}

TEST(mmap_readonly)
{
    void *p;

    p = mmap(NULL, 4096, PROT_READ,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT(p != MAP_FAILED);

    /* reading should work */
    ASSERT_EQ(((unsigned char *)p)[0], 0);

    munmap(p, 4096);
}

/* ===== munmap tests ===== */

TEST(munmap_success)
{
    void *p;
    int ret;

    p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT(p != MAP_FAILED);

    ret = munmap(p, 4096);
    ASSERT_EQ(ret, 0);
}

/* ===== mprotect tests ===== */

TEST(mprotect_readwrite_to_read)
{
    void *p;
    int ret;

    p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT(p != MAP_FAILED);

    /* write first while we can */
    memset(p, 42, 4096);

    /* change to read-only */
    ret = mprotect(p, 4096, PROT_READ);
    ASSERT_EQ(ret, 0);

    /* reading should still work */
    ASSERT_EQ(((unsigned char *)p)[0], 42);

    munmap(p, 4096);
}

TEST(mprotect_restore_write)
{
    void *p;
    int ret;

    p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT(p != MAP_FAILED);

    /* make read-only */
    ret = mprotect(p, 4096, PROT_READ);
    ASSERT_EQ(ret, 0);

    /* restore write permission */
    ret = mprotect(p, 4096, PROT_READ | PROT_WRITE);
    ASSERT_EQ(ret, 0);

    /* writing should work again */
    memset(p, 99, 4096);
    ASSERT_EQ(((unsigned char *)p)[0], 99);

    munmap(p, 4096);
}

int main(void)
{
    printf("test_mman:\n");

    /* mmap */
    RUN_TEST(mmap_anonymous);
    RUN_TEST(mmap_large);
    RUN_TEST(mmap_readonly);

    /* munmap */
    RUN_TEST(munmap_success);

    /* mprotect */
    RUN_TEST(mprotect_readwrite_to_read);
    RUN_TEST(mprotect_restore_write);

    TEST_SUMMARY();
    return tests_failed;
}
