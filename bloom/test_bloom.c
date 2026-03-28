// test_bloom.c — Tests for bloom.h
#define BL_IMPLEMENTATION
#include "bloom.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                             \
    do {                                                                       \
        tests_run++;                                                           \
        printf("  %-50s", name);                                               \
    } while (0)

#define PASS()                                                                 \
    do {                                                                       \
        tests_passed++;                                                        \
        printf("PASS\n");                                                      \
    } while (0)

#define FAIL(msg)                                                              \
    do {                                                                       \
        printf("FAIL: %s\n", msg);                                             \
    } while (0)

#define ASSERT(cond, msg)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

// ── Test: Basic create/free ─────────────────────────────────────────────────

static void test_create_free(void) {
    TEST("bl_create and bl_free");

    bl_filter* f = NULL;
    bl_status s = bl_create(&f, 1000, 0.01);
    ASSERT(s == BL_OK, "bl_create failed");
    ASSERT(f != NULL, "filter is NULL");
    ASSERT(bl_num_hashes(f) > 0, "num_hashes should be > 0");
    ASSERT(bl_size_bytes(f) > 0, "size_bytes should be > 0");
    ASSERT(bl_count(f) == 0, "count should be 0");

    bl_free(f);
    PASS();
}

// ── Test: Add and test items ────────────────────────────────────────────────

static void test_add_and_test(void) {
    TEST("bl_add and bl_test");

    bl_filter* f = NULL;
    bl_status s = bl_create(&f, 1000, 0.01);
    ASSERT(s == BL_OK, "create failed");

    const char* items[] = {"hello", "world", "bloom", "filter", "test"};
    int n = (int)(sizeof(items) / sizeof(items[0]));

    for (int i = 0; i < n; i++) {
        s = bl_add(f, items[i], (int64_t)strlen(items[i]));
        ASSERT(s == BL_OK, "add failed");
    }

    ASSERT(bl_count(f) == n, "count mismatch");

    // All added items must test positive.
    for (int i = 0; i < n; i++) {
        bool found = bl_test(f, items[i], (int64_t)strlen(items[i]));
        ASSERT(found, "added item not found");
    }

    // Items NOT added should (with high probability) test negative.
    const char* absent[] = {"missing", "absent", "nope", "gone", "zilch"};
    int false_positives = 0;
    for (int i = 0; i < (int)(sizeof(absent) / sizeof(absent[0])); i++) {
        if (bl_test(f, absent[i], (int64_t)strlen(absent[i]))) {
            false_positives++;
        }
    }
    // With 1% FPR and only 5 absent items, extremely unlikely to get any FP.
    ASSERT(false_positives <= 1, "too many false positives for small set");

    bl_free(f);
    PASS();
}

// ── Test: bl_add_u64 and bl_test_u64 ───────────────────────────────────────

static void test_u64(void) {
    TEST("bl_add_u64 and bl_test_u64");

    bl_filter* f = NULL;
    bl_status s = bl_create(&f, 10000, 0.01);
    ASSERT(s == BL_OK, "create failed");

    for (uint64_t i = 0; i < 100; i++) {
        s = bl_add_u64(f, i);
        ASSERT(s == BL_OK, "add_u64 failed");
    }

    // All added keys must be found.
    for (uint64_t i = 0; i < 100; i++) {
        ASSERT(bl_test_u64(f, i), "added u64 key not found");
    }

    // Keys not added should mostly be absent.
    int fp = 0;
    for (uint64_t i = 10000; i < 10100; i++) {
        if (bl_test_u64(f, i)) fp++;
    }
    ASSERT(fp <= 5, "too many u64 false positives");

    bl_free(f);
    PASS();
}

// ── Test: bl_add_str and bl_test_str ────────────────────────────────────────

static void test_str(void) {
    TEST("bl_add_str and bl_test_str");

    bl_filter* f = NULL;
    bl_status s = bl_create(&f, 1000, 0.01);
    ASSERT(s == BL_OK, "create failed");

    s = bl_add_str(f, "chunk_0_0_0");
    ASSERT(s == BL_OK, "add_str failed");
    s = bl_add_str(f, "chunk_1_0_0");
    ASSERT(s == BL_OK, "add_str failed");

    ASSERT(bl_test_str(f, "chunk_0_0_0"), "added str not found");
    ASSERT(bl_test_str(f, "chunk_1_0_0"), "added str not found");
    ASSERT(!bl_test_str(f, "chunk_99_99_99"), "absent str found");

    bl_free(f);
    PASS();
}

// ── Test: False positive rate at scale ──────────────────────────────────────

static void test_fpr_at_scale(void) {
    TEST("false positive rate near expected (n=10000, p=0.01)");

    bl_filter* f = NULL;
    int64_t n = 10000;
    double target_fpr = 0.01;
    bl_status s = bl_create(&f, n, target_fpr);
    ASSERT(s == BL_OK, "create failed");

    // Insert n items (use u64 keys 0..n-1).
    for (int64_t i = 0; i < n; i++) {
        uint64_t key = (uint64_t)i;
        s = bl_add(f, &key, (int64_t)sizeof(key));
        ASSERT(s == BL_OK, "add failed");
    }

    // Test 100000 absent items and measure FPR.
    int64_t num_tests = 100000;
    int64_t fp_count = 0;
    for (int64_t i = n; i < n + num_tests; i++) {
        uint64_t key = (uint64_t)i;
        if (bl_test(f, &key, (int64_t)sizeof(key))) {
            fp_count++;
        }
    }

    double measured_fpr = (double)fp_count / (double)num_tests;
    double estimated_fpr = bl_fpr(f);

    printf("[measured=%.4f est=%.4f] ", measured_fpr, estimated_fpr);

    // Allow measured FPR up to 3x the target (generous margin).
    ASSERT(measured_fpr < target_fpr * 3.0,
           "measured FPR too high");

    bl_free(f);
    PASS();
}

// ── Test: Counting filter add/remove ────────────────────────────────────────

static void test_counting_filter(void) {
    TEST("counting filter add/remove");

    bl_filter* f = NULL;
    bl_status s = bl_counting_create(&f, 1000, 0.01);
    ASSERT(s == BL_OK, "counting_create failed");

    s = bl_add_str(f, "alpha");
    ASSERT(s == BL_OK, "add failed");
    s = bl_add_str(f, "beta");
    ASSERT(s == BL_OK, "add failed");

    ASSERT(bl_test_str(f, "alpha"), "alpha should be present");
    ASSERT(bl_test_str(f, "beta"), "beta should be present");
    ASSERT(bl_count(f) == 2, "count should be 2");

    // Remove alpha.
    s = bl_remove(f, "alpha", (int64_t)strlen("alpha"));
    ASSERT(s == BL_OK, "remove failed");
    ASSERT(!bl_test_str(f, "alpha"), "alpha should be absent after remove");
    ASSERT(bl_test_str(f, "beta"), "beta should still be present");
    ASSERT(bl_count(f) == 1, "count should be 1 after remove");

    bl_free(f);
    PASS();
}

// ── Test: Remove on non-counting filter returns error ───────────────────────

static void test_remove_non_counting(void) {
    TEST("bl_remove on non-counting filter returns error");

    bl_filter* f = NULL;
    bl_status s = bl_create(&f, 100, 0.01);
    ASSERT(s == BL_OK, "create failed");

    s = bl_add_str(f, "test");
    ASSERT(s == BL_OK, "add failed");

    s = bl_remove(f, "test", 4);
    ASSERT(s == BL_ERR_NOT_COUNTING, "should return BL_ERR_NOT_COUNTING");

    bl_free(f);
    PASS();
}

// ── Test: Clear resets everything ───────────────────────────────────────────

static void test_clear(void) {
    TEST("bl_clear resets all bits and count");

    bl_filter* f = NULL;
    bl_status s = bl_create(&f, 1000, 0.01);
    ASSERT(s == BL_OK, "create failed");

    for (uint64_t i = 0; i < 50; i++) {
        s = bl_add_u64(f, i);
        ASSERT(s == BL_OK, "add failed");
    }
    ASSERT(bl_count(f) == 50, "count should be 50");

    s = bl_clear(f);
    ASSERT(s == BL_OK, "clear failed");
    ASSERT(bl_count(f) == 0, "count should be 0 after clear");

    // Previously added items should no longer be found.
    int found = 0;
    for (uint64_t i = 0; i < 50; i++) {
        if (bl_test_u64(f, i)) found++;
    }
    ASSERT(found == 0, "items should not be found after clear");

    bl_free(f);
    PASS();
}

// ── Test: Save and load roundtrip ───────────────────────────────────────────

static void test_save_load(void) {
    TEST("bl_save and bl_load roundtrip");

    const char* path = "/tmp/test_bloom_roundtrip.bloom";

    bl_filter* f = NULL;
    bl_status s = bl_create(&f, 1000, 0.01);
    ASSERT(s == BL_OK, "create failed");

    for (uint64_t i = 0; i < 100; i++) {
        s = bl_add_u64(f, i);
        ASSERT(s == BL_OK, "add failed");
    }

    s = bl_save(f, path);
    ASSERT(s == BL_OK, "save failed");

    bl_filter* loaded = NULL;
    s = bl_load(&loaded, path);
    ASSERT(s == BL_OK, "load failed");

    // Verify all items still test positive.
    for (uint64_t i = 0; i < 100; i++) {
        ASSERT(bl_test_u64(loaded, i), "item missing after load");
    }

    // Verify stats match.
    ASSERT(bl_count(loaded) == bl_count(f), "count mismatch after load");
    ASSERT(bl_num_hashes(loaded) == bl_num_hashes(f),
           "num_hashes mismatch after load");
    ASSERT(bl_size_bytes(loaded) == bl_size_bytes(f),
           "size_bytes mismatch after load");

    bl_free(f);
    bl_free(loaded);
    remove(path);
    PASS();
}

// ── Test: Save/load counting filter ─────────────────────────────────────────

static void test_save_load_counting(void) {
    TEST("bl_save and bl_load counting filter");

    const char* path = "/tmp/test_bloom_counting_roundtrip.bloom";

    bl_filter* f = NULL;
    bl_status s = bl_counting_create(&f, 1000, 0.01);
    ASSERT(s == BL_OK, "create failed");

    s = bl_add_str(f, "one");
    ASSERT(s == BL_OK, "add failed");
    s = bl_add_str(f, "two");
    ASSERT(s == BL_OK, "add failed");

    s = bl_save(f, path);
    ASSERT(s == BL_OK, "save failed");

    bl_filter* loaded = NULL;
    s = bl_load(&loaded, path);
    ASSERT(s == BL_OK, "load failed");

    ASSERT(bl_test_str(loaded, "one"), "one missing after load");
    ASSERT(bl_test_str(loaded, "two"), "two missing after load");

    // Counting filter should still support remove after load.
    s = bl_remove(loaded, "one", (int64_t)strlen("one"));
    ASSERT(s == BL_OK, "remove after load failed");
    ASSERT(!bl_test_str(loaded, "one"), "one should be absent after remove");

    bl_free(f);
    bl_free(loaded);
    remove(path);
    PASS();
}

// ── Test: Null argument handling ────────────────────────────────────────────

static void test_null_args(void) {
    TEST("null argument handling");

    ASSERT(bl_create(NULL, 100, 0.01) == BL_ERR_NULL_ARG,
           "bl_create(NULL) should fail");
    ASSERT(bl_counting_create(NULL, 100, 0.01) == BL_ERR_NULL_ARG,
           "bl_counting_create(NULL) should fail");

    bl_filter* f = NULL;
    bl_status s = bl_create(&f, 100, 0.01);
    ASSERT(s == BL_OK, "create failed");

    ASSERT(bl_add(f, NULL, 5) == BL_ERR_NULL_ARG, "add(NULL) should fail");
    ASSERT(bl_add_str(f, NULL) == BL_ERR_NULL_ARG, "add_str(NULL) should fail");
    ASSERT(bl_test(f, NULL, 5) == false, "test(NULL) should return false");
    ASSERT(bl_test_str(f, NULL) == false, "test_str(NULL) should return false");

    bl_free(f);
    PASS();
}

// ── Test: Invalid arguments ─────────────────────────────────────────────────

static void test_invalid_args(void) {
    TEST("invalid argument handling");

    bl_filter* f = NULL;
    ASSERT(bl_create(&f, 0, 0.01) == BL_ERR_INVALID_ARG,
           "zero items should fail");
    ASSERT(bl_create(&f, -1, 0.01) == BL_ERR_INVALID_ARG,
           "negative items should fail");
    ASSERT(bl_create(&f, 100, 0.0) == BL_ERR_INVALID_ARG,
           "zero fpr should fail");
    ASSERT(bl_create(&f, 100, 1.0) == BL_ERR_INVALID_ARG,
           "fpr=1.0 should fail");
    ASSERT(bl_create(&f, 100, -0.5) == BL_ERR_INVALID_ARG,
           "negative fpr should fail");

    PASS();
}

// ── Test: Version and status strings ────────────────────────────────────────

static void test_utilities(void) {
    TEST("bl_version_str and bl_status_str");

    const char* ver = bl_version_str();
    ASSERT(ver != NULL, "version_str should not be NULL");
    ASSERT(strcmp(ver, "0.1.0") == 0, "version should be 0.1.0");

    ASSERT(strcmp(bl_status_str(BL_OK), "BL_OK") == 0,
           "status_str(BL_OK) wrong");
    ASSERT(strlen(bl_status_str(BL_ERR_NULL_ARG)) > 0,
           "status_str should not be empty");
    ASSERT(strlen(bl_status_str(BL_ERR_ALLOC)) > 0,
           "status_str should not be empty");

    PASS();
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(void) {
    printf("bloom.h v%s — test suite\n", bl_version_str());
    printf("─────────────────────────────────────────────────────────────\n");

    test_create_free();
    test_add_and_test();
    test_u64();
    test_str();
    test_fpr_at_scale();
    test_counting_filter();
    test_remove_non_counting();
    test_clear();
    test_save_load();
    test_save_load_counting();
    test_null_args();
    test_invalid_args();
    test_utilities();

    printf("─────────────────────────────────────────────────────────────\n");
    printf("%d / %d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
