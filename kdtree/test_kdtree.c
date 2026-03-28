// test_kdtree.c — Tests for kdtree.h
#define KD_IMPLEMENTATION
#include "kdtree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <float.h>

// ── Test Harness ────────────────────────────────────────────────────────────

static int g_tests_run    = 0;
static int g_tests_passed = 0;

#define TEST(name)                                                     \
    do {                                                               \
        g_tests_run++;                                                 \
        printf("  %-50s ", #name);                                     \
        fflush(stdout);                                                \
    } while (0)

#define PASS()                                                         \
    do { g_tests_passed++; printf("PASS\n"); } while (0)

#define FAIL(msg)                                                      \
    do { printf("FAIL: %s\n", msg); return; } while (0)

#define ASSERT(cond, msg)                                              \
    do { if (!(cond)) { FAIL(msg); } } while (0)

// ── Helper: brute-force nearest ─────────────────────────────────────────────

static double dist_sq(kd_point a, kd_point b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx*dx + dy*dy + dz*dz;
}

static int64_t brute_nearest(const kd_point* pts, int64_t n, kd_point q) {
    int64_t best = 0;
    double best_d = dist_sq(pts[0], q);
    for (int64_t i = 1; i < n; i++) {
        double d = dist_sq(pts[i], q);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

// ── Helper: brute-force KNN ─────────────────────────────────────────────────

static int result_cmp(const void* a, const void* b) {
    double da = ((const kd_result*)a)->dist;
    double db = ((const kd_result*)b)->dist;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

static void brute_knn(const kd_point* pts, int64_t n, kd_point q,
                      int k, kd_result* out, int* found) {
    kd_result* all = (kd_result*)malloc((size_t)n * sizeof(kd_result));
    for (int64_t i = 0; i < n; i++) {
        all[i].index = i;
        all[i].dist = dist_sq(pts[i], q);
    }
    qsort(all, (size_t)n, sizeof(kd_result), result_cmp);
    int cnt = k < (int)n ? k : (int)n;
    memcpy(out, all, (size_t)cnt * sizeof(kd_result));
    *found = cnt;
    free(all);
}

// ── Helper: random point ────────────────────────────────────────────────────

static kd_point rand_point(double range) {
    return (kd_point){
        ((double)rand() / RAND_MAX) * 2.0 * range - range,
        ((double)rand() / RAND_MAX) * 2.0 * range - range,
        ((double)rand() / RAND_MAX) * 2.0 * range - range,
    };
}

// ── Tests ───────────────────────────────────────────────────────────────────

static void test_build_empty(void) {
    TEST(build_empty_returns_error);
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, NULL, 0);
    ASSERT(s == KD_ERR_EMPTY, "expected KD_ERR_EMPTY");
    ASSERT(tree == NULL, "tree should be NULL");
    PASS();
}

static void test_build_null_out(void) {
    TEST(build_null_out_returns_error);
    kd_point p = {1, 2, 3};
    kd_status s = kd_build(NULL, &p, 1);
    ASSERT(s == KD_ERR_NULL_ARG, "expected KD_ERR_NULL_ARG");
    PASS();
}

static void test_build_null_points(void) {
    TEST(build_null_points_returns_error);
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, NULL, 5);
    ASSERT(s == KD_ERR_NULL_ARG, "expected KD_ERR_NULL_ARG");
    PASS();
}

static void test_build_one_point(void) {
    TEST(build_one_point);
    kd_point p = {1.0, 2.0, 3.0};
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, &p, 1);
    ASSERT(s == KD_OK, "build failed");
    ASSERT(kd_size(tree) == 1, "size should be 1");

    kd_point got = kd_point_at(tree, 0);
    ASSERT(got.x == 1.0 && got.y == 2.0 && got.z == 3.0,
           "point mismatch");
    kd_free(tree);
    PASS();
}

static void test_build_1000_points(void) {
    TEST(build_1000_points);
    int64_t n = 1000;
    kd_point* pts = (kd_point*)malloc((size_t)n * sizeof(kd_point));
    for (int64_t i = 0; i < n; i++) pts[i] = rand_point(100.0);

    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, n);
    ASSERT(s == KD_OK, "build failed");
    ASSERT(kd_size(tree) == n, "size mismatch");

    kd_free(tree);
    free(pts);
    PASS();
}

static void test_nearest_one(void) {
    TEST(nearest_one_point);
    kd_point p = {5.0, 5.0, 5.0};
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, &p, 1);
    ASSERT(s == KD_OK, "build failed");

    kd_result res;
    s = kd_nearest(tree, (kd_point){0, 0, 0}, &res);
    ASSERT(s == KD_OK, "nearest failed");
    ASSERT(res.index == 0, "wrong index");
    ASSERT(fabs(res.dist - 75.0) < 1e-9, "wrong distance");

    kd_free(tree);
    PASS();
}

static void test_nearest_exact_match(void) {
    TEST(nearest_exact_match);
    kd_point pts[] = {{1,2,3}, {4,5,6}, {7,8,9}};
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, 3);
    ASSERT(s == KD_OK, "build failed");

    kd_result res;
    s = kd_nearest(tree, (kd_point){4, 5, 6}, &res);
    ASSERT(s == KD_OK, "nearest failed");
    ASSERT(res.index == 1, "wrong index");
    ASSERT(res.dist < 1e-15, "distance should be ~0");

    kd_free(tree);
    PASS();
}

static void test_nearest_brute_force(void) {
    TEST(nearest_vs_brute_force_100pts_100queries);
    int64_t n = 100;
    kd_point* pts = (kd_point*)malloc((size_t)n * sizeof(kd_point));
    for (int64_t i = 0; i < n; i++) pts[i] = rand_point(50.0);

    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, n);
    ASSERT(s == KD_OK, "build failed");

    for (int q = 0; q < 100; q++) {
        kd_point query = rand_point(60.0);
        kd_result res;
        s = kd_nearest(tree, query, &res);
        ASSERT(s == KD_OK, "nearest failed");

        int64_t bf = brute_nearest(pts, n, query);
        double bf_dist = dist_sq(pts[bf], query);
        ASSERT(fabs(res.dist - bf_dist) < 1e-9,
               "nearest disagrees with brute force");
    }

    kd_free(tree);
    free(pts);
    PASS();
}

static void test_nearest_large_brute_force(void) {
    TEST(nearest_vs_brute_force_1000pts_50queries);
    int64_t n = 1000;
    kd_point* pts = (kd_point*)malloc((size_t)n * sizeof(kd_point));
    for (int64_t i = 0; i < n; i++) pts[i] = rand_point(200.0);

    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, n);
    ASSERT(s == KD_OK, "build failed");

    for (int q = 0; q < 50; q++) {
        kd_point query = rand_point(250.0);
        kd_result res;
        s = kd_nearest(tree, query, &res);
        ASSERT(s == KD_OK, "nearest failed");

        int64_t bf = brute_nearest(pts, n, query);
        double bf_dist = dist_sq(pts[bf], query);
        ASSERT(fabs(res.dist - bf_dist) < 1e-9,
               "nearest disagrees with brute force (large)");
    }

    kd_free(tree);
    free(pts);
    PASS();
}

static void test_knn_basic(void) {
    TEST(knn_basic_k3);
    kd_point pts[] = {{0,0,0}, {1,0,0}, {2,0,0}, {10,0,0}, {20,0,0}};
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, 5);
    ASSERT(s == KD_OK, "build failed");

    kd_result results[3];
    int found = 0;
    s = kd_knn(tree, (kd_point){0.5, 0, 0}, 3, results, &found);
    ASSERT(s == KD_OK, "knn failed");
    ASSERT(found == 3, "expected 3 results");

    // Should be indices 0, 1, 2 in some order (sorted by distance)
    ASSERT(results[0].dist <= results[1].dist, "not sorted");
    ASSERT(results[1].dist <= results[2].dist, "not sorted");

    // The three closest to 0.5 are: 0 (d=0.25), 1 (d=0.25), 2 (d=2.25)
    ASSERT(fabs(results[0].dist - 0.25) < 1e-9, "wrong dist[0]");
    ASSERT(fabs(results[1].dist - 0.25) < 1e-9, "wrong dist[1]");
    ASSERT(fabs(results[2].dist - 2.25) < 1e-9, "wrong dist[2]");

    kd_free(tree);
    PASS();
}

static void test_knn_k_exceeds_n(void) {
    TEST(knn_k_exceeds_n);
    kd_point pts[] = {{1,2,3}, {4,5,6}};
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, 2);
    ASSERT(s == KD_OK, "build failed");

    kd_result results[10];
    int found = 0;
    s = kd_knn(tree, (kd_point){0,0,0}, 10, results, &found);
    ASSERT(s == KD_OK, "knn failed");
    ASSERT(found == 2, "expected 2 results");

    kd_free(tree);
    PASS();
}

static void test_knn_brute_force(void) {
    TEST(knn_vs_brute_force_200pts_k5);
    int64_t n = 200;
    int k = 5;
    kd_point* pts = (kd_point*)malloc((size_t)n * sizeof(kd_point));
    for (int64_t i = 0; i < n; i++) pts[i] = rand_point(100.0);

    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, n);
    ASSERT(s == KD_OK, "build failed");

    for (int q = 0; q < 50; q++) {
        kd_point query = rand_point(120.0);

        kd_result kd_res[5];
        int kd_found = 0;
        s = kd_knn(tree, query, k, kd_res, &kd_found);
        ASSERT(s == KD_OK, "knn failed");
        ASSERT(kd_found == k, "should find k results");

        kd_result bf_res[5];
        int bf_found = 0;
        brute_knn(pts, n, query, k, bf_res, &bf_found);

        for (int i = 0; i < k; i++) {
            ASSERT(fabs(kd_res[i].dist - bf_res[i].dist) < 1e-9,
                   "knn distance mismatch with brute force");
        }
    }

    kd_free(tree);
    free(pts);
    PASS();
}

static void test_knn_k_zero(void) {
    TEST(knn_k_zero);
    kd_point p = {1, 2, 3};
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, &p, 1);
    ASSERT(s == KD_OK, "build failed");

    kd_result results[1];
    int found = 99;
    s = kd_knn(tree, (kd_point){0,0,0}, 0, results, &found);
    ASSERT(s == KD_OK, "knn failed");
    ASSERT(found == 0, "expected 0 found");

    kd_free(tree);
    PASS();
}

static void test_radius_basic(void) {
    TEST(radius_basic);
    kd_point pts[] = {{0,0,0}, {1,0,0}, {2,0,0}, {10,0,0}};
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, 4);
    ASSERT(s == KD_OK, "build failed");

    kd_result* results = NULL;
    int64_t count = 0;
    // radius=1.5 from origin: pts[0] (d=0), pts[1] (d=1) should match
    s = kd_radius(tree, (kd_point){0,0,0}, 1.5, &results, &count);
    ASSERT(s == KD_OK, "radius failed");
    ASSERT(count == 2, "expected 2 results");

    // Verify all within radius
    for (int64_t i = 0; i < count; i++) {
        ASSERT(results[i].dist <= 1.5*1.5 + 1e-9, "point outside radius");
    }

    KD_FREE(results);
    kd_free(tree);
    PASS();
}

static void test_radius_none(void) {
    TEST(radius_no_results);
    kd_point pts[] = {{100,100,100}};
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, 1);
    ASSERT(s == KD_OK, "build failed");

    kd_result* results = NULL;
    int64_t count = 0;
    s = kd_radius(tree, (kd_point){0,0,0}, 1.0, &results, &count);
    ASSERT(s == KD_OK, "radius failed");
    ASSERT(count == 0, "expected 0 results");

    KD_FREE(results);
    kd_free(tree);
    PASS();
}

static void test_radius_brute_force(void) {
    TEST(radius_vs_brute_force_300pts);
    int64_t n = 300;
    kd_point* pts = (kd_point*)malloc((size_t)n * sizeof(kd_point));
    for (int64_t i = 0; i < n; i++) pts[i] = rand_point(50.0);

    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, n);
    ASSERT(s == KD_OK, "build failed");

    for (int q = 0; q < 30; q++) {
        kd_point query = rand_point(60.0);
        double radius = 15.0 + ((double)rand() / RAND_MAX) * 20.0;
        double radius_sq = radius * radius;

        kd_result* results = NULL;
        int64_t count = 0;
        s = kd_radius(tree, query, radius, &results, &count);
        ASSERT(s == KD_OK, "radius failed");

        // Brute-force count
        int64_t bf_count = 0;
        for (int64_t i = 0; i < n; i++) {
            if (dist_sq(pts[i], query) <= radius_sq) bf_count++;
        }

        ASSERT(count == bf_count,
               "radius count mismatch with brute force");

        // Verify all results are within radius
        for (int64_t i = 0; i < count; i++) {
            ASSERT(results[i].dist <= radius_sq + 1e-9,
                   "result outside radius");
        }

        // Verify no closer points were missed
        for (int64_t i = 0; i < n; i++) {
            double d = dist_sq(pts[i], query);
            if (d <= radius_sq) {
                bool found_it = false;
                for (int64_t j = 0; j < count; j++) {
                    if (results[j].index == i) { found_it = true; break; }
                }
                ASSERT(found_it, "brute-force point missing from radius results");
            }
        }

        KD_FREE(results);
    }

    kd_free(tree);
    free(pts);
    PASS();
}

static void test_metadata(void) {
    TEST(metadata_size_and_point_at);
    kd_point pts[] = {{1,2,3}, {4,5,6}, {7,8,9}};
    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, 3);
    ASSERT(s == KD_OK, "build failed");

    ASSERT(kd_size(tree) == 3, "size should be 3");
    ASSERT(kd_size(NULL) == 0, "size(NULL) should be 0");

    for (int64_t i = 0; i < 3; i++) {
        kd_point p = kd_point_at(tree, i);
        ASSERT(p.x == pts[i].x && p.y == pts[i].y && p.z == pts[i].z,
               "point_at mismatch");
    }

    // Out of bounds
    kd_point oob = kd_point_at(tree, 99);
    ASSERT(oob.x == 0 && oob.y == 0 && oob.z == 0, "oob should be zero");

    kd_free(tree);
    PASS();
}

static void test_utilities(void) {
    TEST(utilities_status_str_version);
    ASSERT(strcmp(kd_status_str(KD_OK), "ok") == 0, "status_str(OK)");
    ASSERT(strcmp(kd_status_str(KD_ERR_NULL_ARG), "null argument") == 0,
           "status_str(NULL_ARG)");
    ASSERT(strcmp(kd_status_str(KD_ERR_ALLOC), "allocation failed") == 0,
           "status_str(ALLOC)");
    ASSERT(strcmp(kd_status_str(KD_ERR_EMPTY), "empty input") == 0,
           "status_str(EMPTY)");

    const char* v = kd_version_str();
    ASSERT(strcmp(v, "0.1.0") == 0, "version mismatch");
    PASS();
}

static void test_nearest_null_args(void) {
    TEST(nearest_null_args);
    kd_result res;
    kd_status s = kd_nearest(NULL, (kd_point){0,0,0}, &res);
    ASSERT(s == KD_ERR_NULL_ARG, "expected null arg error");

    kd_point p = {1,2,3};
    kd_tree* tree = NULL;
    s = kd_build(&tree, &p, 1);
    ASSERT(s == KD_OK, "build failed");

    s = kd_nearest(tree, (kd_point){0,0,0}, NULL);
    ASSERT(s == KD_ERR_NULL_ARG, "expected null arg error for out");

    kd_free(tree);
    PASS();
}

static void test_knn_null_args(void) {
    TEST(knn_null_args);
    int found;
    kd_result results[1];
    kd_status s = kd_knn(NULL, (kd_point){0,0,0}, 1, results, &found);
    ASSERT(s == KD_ERR_NULL_ARG, "expected null arg error");
    PASS();
}

static void test_radius_null_args(void) {
    TEST(radius_null_args);
    kd_result* results = NULL;
    int64_t count = 0;
    kd_status s = kd_radius(NULL, (kd_point){0,0,0}, 1.0, &results, &count);
    ASSERT(s == KD_ERR_NULL_ARG, "expected null arg error");
    PASS();
}

static void test_negative_n(void) {
    TEST(build_negative_n);
    kd_tree* tree = NULL;
    kd_point p = {1,2,3};
    kd_status s = kd_build(&tree, &p, -1);
    ASSERT(s == KD_ERR_EMPTY, "expected KD_ERR_EMPTY for negative n");
    PASS();
}

static void test_duplicate_points(void) {
    TEST(duplicate_points);
    kd_point pts[50];
    for (int i = 0; i < 50; i++) pts[i] = (kd_point){1.0, 2.0, 3.0};

    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, 50);
    ASSERT(s == KD_OK, "build with duplicates failed");

    kd_result res;
    s = kd_nearest(tree, (kd_point){1.0, 2.0, 3.0}, &res);
    ASSERT(s == KD_OK, "nearest failed");
    ASSERT(res.dist < 1e-15, "should match exactly");

    kd_free(tree);
    PASS();
}

static void test_collinear_points(void) {
    TEST(collinear_points);
    int64_t n = 100;
    kd_point* pts = (kd_point*)malloc((size_t)n * sizeof(kd_point));
    for (int64_t i = 0; i < n; i++) {
        pts[i] = (kd_point){(double)i, 0.0, 0.0};
    }

    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, n);
    ASSERT(s == KD_OK, "build failed");

    kd_result res;
    s = kd_nearest(tree, (kd_point){50.5, 0.0, 0.0}, &res);
    ASSERT(s == KD_OK, "nearest failed");
    ASSERT(res.index == 50 || res.index == 51, "wrong nearest for collinear");

    kd_free(tree);
    free(pts);
    PASS();
}

static void test_knn_large_brute_force(void) {
    TEST(knn_vs_brute_force_500pts_k10);
    int64_t n = 500;
    int k = 10;
    kd_point* pts = (kd_point*)malloc((size_t)n * sizeof(kd_point));
    for (int64_t i = 0; i < n; i++) pts[i] = rand_point(200.0);

    kd_tree* tree = NULL;
    kd_status s = kd_build(&tree, pts, n);
    ASSERT(s == KD_OK, "build failed");

    for (int q = 0; q < 30; q++) {
        kd_point query = rand_point(250.0);

        kd_result kd_res[10];
        int kd_found = 0;
        s = kd_knn(tree, query, k, kd_res, &kd_found);
        ASSERT(s == KD_OK, "knn failed");
        ASSERT(kd_found == k, "should find k results");

        kd_result bf_res[10];
        int bf_found = 0;
        brute_knn(pts, n, query, k, bf_res, &bf_found);

        for (int i = 0; i < k; i++) {
            ASSERT(fabs(kd_res[i].dist - bf_res[i].dist) < 1e-9,
                   "knn distance mismatch (large)");
        }
    }

    kd_free(tree);
    free(pts);
    PASS();
}

static void test_free_null(void) {
    TEST(free_null_is_safe);
    kd_free(NULL);  // should not crash
    PASS();
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(void) {
    srand(42);  // deterministic

    printf("kdtree.h v%s tests\n", kd_version_str());
    printf("─────────────────────────────────────────────────────────────\n");

    // Build / error tests
    test_build_empty();
    test_build_null_out();
    test_build_null_points();
    test_build_one_point();
    test_build_1000_points();
    test_negative_n();

    // Nearest neighbor
    test_nearest_one();
    test_nearest_exact_match();
    test_nearest_brute_force();
    test_nearest_large_brute_force();
    test_nearest_null_args();

    // KNN
    test_knn_basic();
    test_knn_k_exceeds_n();
    test_knn_brute_force();
    test_knn_k_zero();
    test_knn_null_args();
    test_knn_large_brute_force();

    // Radius
    test_radius_basic();
    test_radius_none();
    test_radius_brute_force();
    test_radius_null_args();

    // Metadata & utilities
    test_metadata();
    test_utilities();
    test_free_null();

    // Edge cases
    test_duplicate_points();
    test_collinear_points();

    printf("─────────────────────────────────────────────────────────────\n");
    printf("%d / %d tests passed\n", g_tests_passed, g_tests_run);

    return g_tests_passed == g_tests_run ? 0 : 1;
}
