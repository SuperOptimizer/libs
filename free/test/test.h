/*
 * test.h - Minimal test framework for the free toolchain.
 * Pure C89. No external dependencies beyond stdio, string, signal, setjmp.
 * Includes per-test timeout support via SIGALRM and memory limits via
 * setrlimit to prevent runaway allocations from killing the system.
 */

#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/resource.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int tests_timeout = 0;
static int current_failed = 0;
static int test_timeout_sec = 10;   /* default 10s per test */
static jmp_buf test_jmp;

/* Default memory limit: 256 MB virtual address space.
 * Override with TEST_SET_MEMLIMIT(bytes) before RUN_TEST. */
static unsigned long test_memlimit = 256UL * 1024 * 1024;
static int test_memlimit_applied = 0;

static void test_apply_memlimit(void)
{
    struct rlimit rl;
    if (test_memlimit_applied) return;
    rl.rlim_cur = test_memlimit;
    rl.rlim_max = test_memlimit;
    setrlimit(RLIMIT_AS, &rl);
    test_memlimit_applied = 1;
}

static void test_alarm_handler(int sig)
{
    (void)sig;
    longjmp(test_jmp, 1);
}

#define TEST(name) static void test_##name(void)

#define TEST_SET_TIMEOUT(sec) do { test_timeout_sec = (sec); } while(0)
#define TEST_SET_MEMLIMIT(bytes) do { \
    test_memlimit = (unsigned long)(bytes); \
    test_memlimit_applied = 0; \
} while(0)

#define RUN_TEST(name) do { \
    current_failed = 0; \
    test_apply_memlimit(); \
    signal(SIGALRM, test_alarm_handler); \
    if (setjmp(test_jmp) == 0) { \
        alarm((unsigned)test_timeout_sec); \
        test_##name(); \
        alarm(0); \
    } else { \
        printf("  TIMEOUT %s (>%ds)\n", #name, test_timeout_sec); \
        current_failed = 1; \
        tests_timeout++; \
    } \
    signal(SIGALRM, SIG_DFL); \
    tests_run++; \
    if (current_failed) { \
        tests_failed++; \
    } else { \
        tests_passed++; \
        printf("  PASS %s\n", #name); \
    } \
} while(0)

#define ASSERT(expr) do { \
    if (!(expr)) { \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        current_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    long _a = (long)(a), _b = (long)(b); \
    if (_a != _b) { \
        printf("  FAIL %s:%d: %s == %s (%ld != %ld)\n", \
            __FILE__, __LINE__, #a, #b, _a, _b); \
        current_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NE(a, b) do { \
    long _a = (long)(a), _b = (long)(b); \
    if (_a == _b) { \
        printf("  FAIL %s:%d: %s != %s (both %ld)\n", \
            __FILE__, __LINE__, #a, #b, _a); \
        current_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    const char *_a = (a), *_b = (b); \
    if (strcmp(_a, _b) != 0) { \
        printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", \
            __FILE__, __LINE__, _a, _b); \
        current_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_LT(a, b) ASSERT((a) < (b))
#define ASSERT_LE(a, b) ASSERT((a) <= (b))
#define ASSERT_GT(a, b) ASSERT((a) > (b))
#define ASSERT_GE(a, b) ASSERT((a) >= (b))

#define TEST_SUMMARY() do { \
    printf("\n%d tests: %d passed, %d failed", \
        tests_run, tests_passed, tests_failed); \
    if (tests_timeout > 0) \
        printf(", %d timeouts", tests_timeout); \
    printf("\n"); \
} while(0)

#endif
