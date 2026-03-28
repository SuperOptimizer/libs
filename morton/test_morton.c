#define MT_IMPLEMENTATION
#include "morton.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void name(void)
#define ASSERT(cond)                                                          \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_fail++;                                                         \
            return;                                                           \
        }                                                                     \
    } while (0)

#define RUN(fn)                          \
    do {                                 \
        printf("  %-50s", #fn);          \
        fn();                            \
        if (g_fail == 0) {               \
            printf("ok\n");              \
            g_pass++;                    \
        } else {                         \
            printf("FAILED\n");          \
        }                                \
    } while (0)

// Use a variant that tracks per-test failures properly.
#undef RUN
#define RUN(fn)                                \
    do {                                       \
        int before = g_fail;                   \
        printf("  %-50s", #fn);                \
        fn();                                  \
        if (g_fail == before) {                \
            printf("ok\n");                    \
            g_pass++;                          \
        } else {                               \
            printf("FAILED\n");                \
        }                                      \
    } while (0)

// ── 2D Known Values ────────────────────────────────────────────────────────

TEST(test_encode_2d_known) {
    ASSERT(mt_encode_2d(0, 0) == 0);
    ASSERT(mt_encode_2d(1, 0) == 1);
    ASSERT(mt_encode_2d(0, 1) == 2);
    ASSERT(mt_encode_2d(1, 1) == 3);
    ASSERT(mt_encode_2d(2, 0) == 4);
    ASSERT(mt_encode_2d(0, 2) == 8);
    ASSERT(mt_encode_2d(3, 3) == 15);
}

// ── 2D Roundtrip ───────────────────────────────────────────────────────────

TEST(test_encode_decode_2d_roundtrip) {
    for (uint32_t y = 0; y < 256; y++) {
        for (uint32_t x = 0; x < 256; x++) {
            uint32_t code = mt_encode_2d((uint16_t)x, (uint16_t)y);
            uint16_t dx, dy;
            mt_decode_2d(code, &dx, &dy);
            ASSERT(dx == x);
            ASSERT(dy == y);
        }
    }
}

// ── 2D 64-bit Roundtrip ───────────────────────────────────────────────────

TEST(test_encode_decode_2d_64_roundtrip) {
    // Test large coordinates.
    uint32_t vals[] = {0, 1, 255, 256, 1023, 65535, 100000, 0xFFFF, 0x10000, 0xFFFFF};
    int n = (int)(sizeof(vals) / sizeof(vals[0]));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            uint64_t code = mt_encode_2d_64(vals[i], vals[j]);
            uint32_t dx, dy;
            mt_decode_2d_64(code, &dx, &dy);
            ASSERT(dx == vals[i]);
            ASSERT(dy == vals[j]);
        }
    }
}

// ── 3D Known Values ────────────────────────────────────────────────────────

TEST(test_encode_3d_known) {
    ASSERT(mt_encode_3d(0, 0, 0) == 0);
    ASSERT(mt_encode_3d(1, 0, 0) == 1);
    ASSERT(mt_encode_3d(0, 1, 0) == 2);
    ASSERT(mt_encode_3d(0, 0, 1) == 4);
    ASSERT(mt_encode_3d(1, 1, 0) == 3);
    ASSERT(mt_encode_3d(1, 0, 1) == 5);
    ASSERT(mt_encode_3d(0, 1, 1) == 6);
    ASSERT(mt_encode_3d(1, 1, 1) == 7);
}

// ── 3D Roundtrip ───────────────────────────────────────────────────────────

TEST(test_encode_decode_3d_roundtrip) {
    for (uint32_t z = 0; z < 32; z++) {
        for (uint32_t y = 0; y < 32; y++) {
            for (uint32_t x = 0; x < 32; x++) {
                uint32_t code = mt_encode_3d((uint16_t)x, (uint16_t)y, (uint16_t)z);
                uint16_t dx, dy, dz;
                mt_decode_3d(code, &dx, &dy, &dz);
                ASSERT(dx == x);
                ASSERT(dy == y);
                ASSERT(dz == z);
            }
        }
    }
}

// ── 3D Boundary ────────────────────────────────────────────────────────────

TEST(test_encode_decode_3d_boundary) {
    // Test max 10-bit values (1023).
    uint16_t vals[] = {0, 1, 511, 512, 1023};
    int n = (int)(sizeof(vals) / sizeof(vals[0]));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            for (int k = 0; k < n; k++) {
                uint32_t code = mt_encode_3d(vals[i], vals[j], vals[k]);
                uint16_t dx, dy, dz;
                mt_decode_3d(code, &dx, &dy, &dz);
                ASSERT(dx == vals[i]);
                ASSERT(dy == vals[j]);
                ASSERT(dz == vals[k]);
            }
        }
    }
}

// ── 3D 64-bit Roundtrip ───────────────────────────────────────────────────

TEST(test_encode_decode_3d_64_roundtrip) {
    uint32_t vals[] = {0, 1, 1023, 1024, 65535, 100000, 0x1FFFFF};
    int n = (int)(sizeof(vals) / sizeof(vals[0]));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            for (int k = 0; k < n; k++) {
                uint64_t code = mt_encode_3d_64(vals[i], vals[j], vals[k]);
                uint32_t dx, dy, dz;
                mt_decode_3d_64(code, &dx, &dy, &dz);
                ASSERT(dx == vals[i]);
                ASSERT(dy == vals[j]);
                ASSERT(dz == vals[k]);
            }
        }
    }
}

// ── 2D Iterator: Power-of-2 Grid ──────────────────────────────────────────

TEST(test_iter2d_power_of_2) {
    uint16_t nx = 4, ny = 4;
    uint32_t total = (uint32_t)nx * ny;
    // Track which cells are visited.
    bool visited[4][4];
    memset(visited, 0, sizeof(visited));

    uint32_t count = 0;
    for (mt_iter2d it = mt_iter2d_start(nx, ny); mt_iter2d_valid(&it); mt_iter2d_next(&it)) {
        ASSERT(it.x < nx);
        ASSERT(it.y < ny);
        ASSERT(!visited[it.y][it.x]);
        visited[it.y][it.x] = true;
        count++;
    }
    ASSERT(count == total);

    // Verify all cells visited.
    for (int y = 0; y < ny; y++)
        for (int x = 0; x < nx; x++)
            ASSERT(visited[y][x]);
}

// ── 2D Iterator: Non-Power-of-2 Grid ──────────────────────────────────────

TEST(test_iter2d_non_power_of_2) {
    uint16_t nx = 3, ny = 5;
    uint32_t total = (uint32_t)nx * ny;
    bool visited[5][3];
    memset(visited, 0, sizeof(visited));

    uint32_t count = 0;
    for (mt_iter2d it = mt_iter2d_start(nx, ny); mt_iter2d_valid(&it); mt_iter2d_next(&it)) {
        ASSERT(it.x < nx);
        ASSERT(it.y < ny);
        ASSERT(!visited[it.y][it.x]);
        visited[it.y][it.x] = true;
        count++;
    }
    ASSERT(count == total);
}

// ── 3D Iterator: Power-of-2 Grid ──────────────────────────────────────────

TEST(test_iter3d_power_of_2) {
    uint16_t nx = 4, ny = 4, nz = 4;
    uint32_t total = (uint32_t)nx * ny * nz;
    bool visited[4][4][4];
    memset(visited, 0, sizeof(visited));

    uint32_t count = 0;
    for (mt_iter3d it = mt_iter3d_start(nx, ny, nz); mt_iter3d_valid(&it); mt_iter3d_next(&it)) {
        ASSERT(it.x < nx);
        ASSERT(it.y < ny);
        ASSERT(it.z < nz);
        ASSERT(!visited[it.z][it.y][it.x]);
        visited[it.z][it.y][it.x] = true;
        count++;
    }
    ASSERT(count == total);
}

// ── 3D Iterator: Non-Power-of-2 Grid ──────────────────────────────────────

TEST(test_iter3d_non_power_of_2) {
    uint16_t nx = 3, ny = 5, nz = 7;
    uint32_t total = (uint32_t)nx * ny * nz;
    bool visited[7][5][3];
    memset(visited, 0, sizeof(visited));

    uint32_t count = 0;
    for (mt_iter3d it = mt_iter3d_start(nx, ny, nz); mt_iter3d_valid(&it); mt_iter3d_next(&it)) {
        ASSERT(it.x < nx);
        ASSERT(it.y < ny);
        ASSERT(it.z < nz);
        ASSERT(!visited[it.z][it.y][it.x]);
        visited[it.z][it.y][it.x] = true;
        count++;
    }
    ASSERT(count == total);
}

// ── 3D Iterator: 1x1x1 Grid ──────────────────────────────────────────────

TEST(test_iter3d_1x1x1) {
    uint32_t count = 0;
    for (mt_iter3d it = mt_iter3d_start(1, 1, 1); mt_iter3d_valid(&it); mt_iter3d_next(&it)) {
        ASSERT(it.x == 0);
        ASSERT(it.y == 0);
        ASSERT(it.z == 0);
        count++;
    }
    ASSERT(count == 1);
}

// ── Compare ────────────────────────────────────────────────────────────────

TEST(test_compare_3d) {
    // (0,0,0) < (1,0,0)
    ASSERT(mt_compare_3d(0, 0, 0, 1, 0, 0) < 0);
    // (1,0,0) > (0,0,0)
    ASSERT(mt_compare_3d(1, 0, 0, 0, 0, 0) > 0);
    // Equal.
    ASSERT(mt_compare_3d(5, 3, 7, 5, 3, 7) == 0);
    // (1,0,0) code=1, (0,1,0) code=2 => (1,0,0) < (0,1,0)
    ASSERT(mt_compare_3d(1, 0, 0, 0, 1, 0) < 0);
    // (0,0,1) code=4, (1,1,0) code=3 => (1,1,0) < (0,0,1)
    ASSERT(mt_compare_3d(1, 1, 0, 0, 0, 1) < 0);
}

// ── Version ────────────────────────────────────────────────────────────────

TEST(test_version) {
    ASSERT(strcmp(mt_version_str(), "0.1.0") == 0);
}

// ── Main ───────────────────────────────────────────────────────────────────

int main(void) {
    printf("morton.h test suite\n");

    RUN(test_encode_2d_known);
    RUN(test_encode_decode_2d_roundtrip);
    RUN(test_encode_decode_2d_64_roundtrip);
    RUN(test_encode_3d_known);
    RUN(test_encode_decode_3d_roundtrip);
    RUN(test_encode_decode_3d_boundary);
    RUN(test_encode_decode_3d_64_roundtrip);
    RUN(test_iter2d_power_of_2);
    RUN(test_iter2d_non_power_of_2);
    RUN(test_iter3d_power_of_2);
    RUN(test_iter3d_non_power_of_2);
    RUN(test_iter3d_1x1x1);
    RUN(test_compare_3d);
    RUN(test_version);

    printf("\n  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
