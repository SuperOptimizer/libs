#define TF_IMPLEMENTATION
#include "tiff.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-50s", name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
    } while (0)

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); return; } \
    } while (0)

// ── Utilities ───────────────────────────────────────────────────────────────

static void test_version(void)
{
    TEST("tf_version_str");
    const char* v = tf_version_str();
    ASSERT(strcmp(v, "0.1.0") == 0, "expected 0.1.0");
    PASS();
}

static void test_status_str(void)
{
    TEST("tf_status_str");
    ASSERT(strcmp(tf_status_str(TF_OK), "ok") == 0, "TF_OK");
    ASSERT(strcmp(tf_status_str(TF_ERR_IO), "I/O error") == 0, "TF_ERR_IO");
    PASS();
}

static void test_dtype_str(void)
{
    TEST("tf_dtype_str");
    ASSERT(strcmp(tf_dtype_str(TF_U8), "u8") == 0, "u8");
    ASSERT(strcmp(tf_dtype_str(TF_U16), "u16") == 0, "u16");
    ASSERT(strcmp(tf_dtype_str(TF_F32), "f32") == 0, "f32");
    PASS();
}

static void test_dtype_size(void)
{
    TEST("tf_dtype_size");
    ASSERT(tf_dtype_size(TF_U8)  == 1, "u8 size");
    ASSERT(tf_dtype_size(TF_U16) == 2, "u16 size");
    ASSERT(tf_dtype_size(TF_F32) == 4, "f32 size");
    PASS();
}

// ── Image Lifecycle ─────────────────────────────────────────────────────────

static void test_create_image(void)
{
    TEST("tf_image_create");
    tf_image* img = NULL;
    tf_status s = tf_image_create(64, 32, TF_U8, &img);
    ASSERT(s == TF_OK, tf_status_str(s));
    ASSERT(img != NULL, "img null");
    ASSERT(tf_image_width(img) == 64, "width");
    ASSERT(tf_image_height(img) == 32, "height");
    ASSERT(tf_image_dtype(img) == TF_U8, "dtype");
    ASSERT(tf_image_pixel_size(img) == 1, "pixel_size");
    ASSERT(tf_image_data(img) != NULL, "data null");
    tf_image_free(img);
    PASS();
}

static void test_from_data(void)
{
    TEST("tf_image_from_data");
    uint16_t buf[4] = {100, 200, 300, 400};
    tf_image* img = NULL;
    tf_status s = tf_image_from_data(buf, 2, 2, TF_U16, &img);
    ASSERT(s == TF_OK, tf_status_str(s));
    ASSERT(tf_image_data(img) == buf, "data pointer should match");
    ASSERT(tf_get_u16(img, 1, 1) == 400, "pixel value");
    tf_image_free(img); // should NOT free buf
    PASS();
}

static void test_clone(void)
{
    TEST("tf_image_clone");
    tf_image* src = NULL;
    (void)tf_image_create(4, 4, TF_F32, &src);
    tf_set_f32(src, 2, 3, 3.14f);

    tf_image* dst = NULL;
    tf_status s = tf_image_clone(&dst, src);
    ASSERT(s == TF_OK, tf_status_str(s));
    ASSERT(tf_image_data(dst) != tf_image_data(src), "should be deep copy");
    ASSERT(fabsf(tf_get_f32(dst, 2, 3) - 3.14f) < 1e-6f, "cloned value");

    tf_image_free(src);
    tf_image_free(dst);
    PASS();
}

// ── Pixel Access ────────────────────────────────────────────────────────────

static void test_pixel_u8(void)
{
    TEST("pixel access u8");
    tf_image* img = NULL;
    (void)tf_image_create(8, 8, TF_U8, &img);
    tf_set_u8(img, 3, 5, 42);
    ASSERT(tf_get_u8(img, 3, 5) == 42, "get/set u8");
    ASSERT(tf_get_u8(img, 0, 0) == 0, "zero-init");
    tf_image_free(img);
    PASS();
}

static void test_pixel_u16(void)
{
    TEST("pixel access u16");
    tf_image* img = NULL;
    (void)tf_image_create(8, 8, TF_U16, &img);
    tf_set_u16(img, 7, 7, 65535);
    ASSERT(tf_get_u16(img, 7, 7) == 65535, "get/set u16");
    tf_image_free(img);
    PASS();
}

static void test_pixel_f32(void)
{
    TEST("pixel access f32");
    tf_image* img = NULL;
    (void)tf_image_create(8, 8, TF_F32, &img);
    tf_set_f32(img, 0, 0, -1.5f);
    tf_set_f32(img, 7, 7, 999.125f);
    ASSERT(fabsf(tf_get_f32(img, 0, 0) - (-1.5f)) < 1e-6f, "get/set f32 neg");
    ASSERT(fabsf(tf_get_f32(img, 7, 7) - 999.125f) < 1e-6f, "get/set f32 large");
    tf_image_free(img);
    PASS();
}

static void test_pixel_bounds(void)
{
    TEST("pixel access out of bounds");
    tf_image* img = NULL;
    (void)tf_image_create(4, 4, TF_U8, &img);
    tf_set_u8(img, -1, 0, 99);   // should be no-op
    tf_set_u8(img, 4, 0, 99);    // should be no-op
    ASSERT(tf_get_u8(img, -1, 0) == 0, "oob get returns 0");
    ASSERT(tf_get_u8(img, 4, 0) == 0, "oob get returns 0");
    tf_image_free(img);
    PASS();
}

// ── Roundtrip: Write then Read ──────────────────────────────────────────────

static void test_roundtrip_u8(void)
{
    TEST("roundtrip u8");
    const char* path = "/tmp/test_tiff_u8.tif";
    int w = 16, h = 32;

    tf_image* src = NULL;
    (void)tf_image_create(w, h, TF_U8, &src);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            tf_set_u8(src, x, y, (uint8_t)((x + y * w) & 0xFF));

    tf_status s = tf_write(path, src);
    ASSERT(s == TF_OK, "write failed");

    tf_image* dst = NULL;
    s = tf_read(path, &dst);
    ASSERT(s == TF_OK, "read failed");
    ASSERT(tf_image_width(dst) == w, "width mismatch");
    ASSERT(tf_image_height(dst) == h, "height mismatch");
    ASSERT(tf_image_dtype(dst) == TF_U8, "dtype mismatch");

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            ASSERT(tf_get_u8(dst, x, y) == (uint8_t)((x + y * w) & 0xFF), "pixel mismatch");

    tf_image_free(src);
    tf_image_free(dst);
    remove(path);
    PASS();
}

static void test_roundtrip_u16(void)
{
    TEST("roundtrip u16");
    const char* path = "/tmp/test_tiff_u16.tif";
    int w = 20, h = 10;

    tf_image* src = NULL;
    (void)tf_image_create(w, h, TF_U16, &src);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            tf_set_u16(src, x, y, (uint16_t)(x * 1000 + y));

    tf_status s = tf_write(path, src);
    ASSERT(s == TF_OK, "write failed");

    tf_image* dst = NULL;
    s = tf_read(path, &dst);
    ASSERT(s == TF_OK, "read failed");
    ASSERT(tf_image_dtype(dst) == TF_U16, "dtype mismatch");

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            ASSERT(tf_get_u16(dst, x, y) == (uint16_t)(x * 1000 + y), "pixel mismatch");

    tf_image_free(src);
    tf_image_free(dst);
    remove(path);
    PASS();
}

static void test_roundtrip_f32(void)
{
    TEST("roundtrip f32");
    const char* path = "/tmp/test_tiff_f32.tif";
    int w = 64, h = 32;

    tf_image* src = NULL;
    (void)tf_image_create(w, h, TF_F32, &src);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            tf_set_f32(src, x, y, (float)x * 0.5f + (float)y * 100.0f);

    tf_status s = tf_write(path, src);
    ASSERT(s == TF_OK, "write failed");

    tf_image* dst = NULL;
    s = tf_read(path, &dst);
    ASSERT(s == TF_OK, "read failed");
    ASSERT(tf_image_dtype(dst) == TF_F32, "dtype mismatch");

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float expected = (float)x * 0.5f + (float)y * 100.0f;
            ASSERT(fabsf(tf_get_f32(dst, x, y) - expected) < 1e-6f, "pixel mismatch");
        }

    tf_image_free(src);
    tf_image_free(dst);
    remove(path);
    PASS();
}

// ── Header-Only Read ────────────────────────────────────────────────────────

static void test_read_header(void)
{
    TEST("tf_read_header");
    const char* path = "/tmp/test_tiff_hdr.tif";

    tf_image* img = NULL;
    (void)tf_image_create(100, 200, TF_F32, &img);
    (void)tf_write(path, img);
    tf_image_free(img);

    int w = 0, h = 0;
    tf_dtype dt = TF_U8;
    tf_status s = tf_read_header(path, &w, &h, &dt);
    ASSERT(s == TF_OK, tf_status_str(s));
    ASSERT(w == 100, "width");
    ASSERT(h == 200, "height");
    ASSERT(dt == TF_F32, "dtype");

    remove(path);
    PASS();
}

// ── Multi-Image Stack ───────────────────────────────────────────────────────

static void test_stack_roundtrip(void)
{
    TEST("stack roundtrip (3 images)");
    const char* path = "/tmp/test_tiff_stack.tif";

    tf_image* imgs[3] = {NULL, NULL, NULL};
    for (int i = 0; i < 3; ++i) {
        (void)tf_image_create(8, 8, TF_U8, &imgs[i]);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                tf_set_u8(imgs[i], x, y, (uint8_t)(i * 64 + y * 8 + x));
    }

    tf_status s = tf_write_stack(path, (const tf_image**)imgs, 3);
    ASSERT(s == TF_OK, "write_stack failed");

    tf_image** read_imgs = NULL;
    int count = 0;
    s = tf_read_stack(path, &read_imgs, &count);
    ASSERT(s == TF_OK, "read_stack failed");
    ASSERT(count == 3, "expected 3 images");

    for (int i = 0; i < 3; ++i) {
        ASSERT(tf_image_width(read_imgs[i]) == 8, "width");
        ASSERT(tf_image_height(read_imgs[i]) == 8, "height");
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                ASSERT(tf_get_u8(read_imgs[i], x, y) ==
                       (uint8_t)(i * 64 + y * 8 + x), "pixel mismatch in stack");
    }

    for (int i = 0; i < 3; ++i) tf_image_free(imgs[i]);
    for (int i = 0; i < count; ++i) tf_image_free(read_imgs[i]);
    TF_FREE(read_imgs);
    remove(path);
    PASS();
}

static void test_stack_mixed_dtypes(void)
{
    TEST("stack mixed dtypes");
    const char* path = "/tmp/test_tiff_stack_mix.tif";

    tf_image* imgs[3] = {NULL, NULL, NULL};
    (void)tf_image_create(4, 4, TF_U8,  &imgs[0]);
    (void)tf_image_create(4, 4, TF_U16, &imgs[1]);
    (void)tf_image_create(4, 4, TF_F32, &imgs[2]);

    tf_set_u8(imgs[0], 0, 0, 255);
    tf_set_u16(imgs[1], 0, 0, 12345);
    tf_set_f32(imgs[2], 0, 0, 1.5f);

    tf_status s = tf_write_stack(path, (const tf_image**)imgs, 3);
    ASSERT(s == TF_OK, "write_stack failed");

    tf_image** read_imgs = NULL;
    int count = 0;
    s = tf_read_stack(path, &read_imgs, &count);
    ASSERT(s == TF_OK, "read_stack failed");
    ASSERT(count == 3, "count");

    ASSERT(tf_image_dtype(read_imgs[0]) == TF_U8, "dtype[0]");
    ASSERT(tf_image_dtype(read_imgs[1]) == TF_U16, "dtype[1]");
    ASSERT(tf_image_dtype(read_imgs[2]) == TF_F32, "dtype[2]");

    ASSERT(tf_get_u8(read_imgs[0], 0, 0) == 255, "val[0]");
    ASSERT(tf_get_u16(read_imgs[1], 0, 0) == 12345, "val[1]");
    ASSERT(fabsf(tf_get_f32(read_imgs[2], 0, 0) - 1.5f) < 1e-6f, "val[2]");

    for (int i = 0; i < 3; ++i) tf_image_free(imgs[i]);
    for (int i = 0; i < count; ++i) tf_image_free(read_imgs[i]);
    TF_FREE(read_imgs);
    remove(path);
    PASS();
}

// ── Error Cases ─────────────────────────────────────────────────────────────

static void test_error_null_args(void)
{
    TEST("error: null arguments");
    ASSERT(tf_image_create(8, 8, TF_U8, NULL) == TF_ERR_NULL_ARG, "null out");
    ASSERT(tf_image_from_data(NULL, 8, 8, TF_U8, NULL) == TF_ERR_NULL_ARG, "null data+out");
    ASSERT(tf_read(NULL, NULL) == TF_ERR_NULL_ARG, "null path");
    ASSERT(tf_write(NULL, NULL) == TF_ERR_NULL_ARG, "null path+img");
    PASS();
}

static void test_error_bad_dims(void)
{
    TEST("error: invalid dimensions");
    tf_image* img = NULL;
    ASSERT(tf_image_create(0, 8, TF_U8, &img) == TF_ERR_BOUNDS, "zero width");
    ASSERT(tf_image_create(8, -1, TF_U8, &img) == TF_ERR_BOUNDS, "negative height");
    PASS();
}

static void test_error_bad_file(void)
{
    TEST("error: nonexistent file");
    tf_image* img = NULL;
    ASSERT(tf_read("/tmp/nonexistent_tiff_test_file.tif", &img) == TF_ERR_IO, "should fail");
    PASS();
}

static void test_error_bad_format(void)
{
    TEST("error: invalid TIFF format");
    const char* path = "/tmp/test_tiff_bad.tif";
    FILE* f = fopen(path, "wb");
    fprintf(f, "NOT A TIFF FILE");
    fclose(f);

    tf_image* img = NULL;
    tf_status s = tf_read(path, &img);
    ASSERT(s != TF_OK, "should fail on garbage data");
    remove(path);
    PASS();
}

// ── 1x1 Image Edge Case ────────────────────────────────────────────────────

static void test_1x1_image(void)
{
    TEST("1x1 image roundtrip");
    const char* path = "/tmp/test_tiff_1x1.tif";

    tf_image* src = NULL;
    (void)tf_image_create(1, 1, TF_F32, &src);
    tf_set_f32(src, 0, 0, 42.0f);

    tf_status s = tf_write(path, src);
    ASSERT(s == TF_OK, "write failed");

    tf_image* dst = NULL;
    s = tf_read(path, &dst);
    ASSERT(s == TF_OK, "read failed");
    ASSERT(fabsf(tf_get_f32(dst, 0, 0) - 42.0f) < 1e-6f, "pixel value");

    tf_image_free(src);
    tf_image_free(dst);
    remove(path);
    PASS();
}

// ── Large Image ─────────────────────────────────────────────────────────────

static void test_large_image(void)
{
    TEST("large image (1024x1024 u16)");
    const char* path = "/tmp/test_tiff_large.tif";
    int w = 1024, h = 1024;

    tf_image* src = NULL;
    (void)tf_image_create(w, h, TF_U16, &src);
    for (int y = 0; y < h; y += 100)
        for (int x = 0; x < w; x += 100)
            tf_set_u16(src, x, y, (uint16_t)(x + y));

    tf_status s = tf_write(path, src);
    ASSERT(s == TF_OK, "write");

    tf_image* dst = NULL;
    s = tf_read(path, &dst);
    ASSERT(s == TF_OK, "read");

    for (int y = 0; y < h; y += 100)
        for (int x = 0; x < w; x += 100)
            ASSERT(tf_get_u16(dst, x, y) == (uint16_t)(x + y), "pixel mismatch");

    tf_image_free(src);
    tf_image_free(dst);
    remove(path);
    PASS();
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(void)
{
    printf("libtiff %s tests\n", tf_version_str());
    printf("────────────────────────────────────────────────────────────\n");

    // Utilities
    test_version();
    test_status_str();
    test_dtype_str();
    test_dtype_size();

    // Image lifecycle
    test_create_image();
    test_from_data();
    test_clone();

    // Pixel access
    test_pixel_u8();
    test_pixel_u16();
    test_pixel_f32();
    test_pixel_bounds();

    // Roundtrip
    test_roundtrip_u8();
    test_roundtrip_u16();
    test_roundtrip_f32();

    // Header read
    test_read_header();

    // Stacks
    test_stack_roundtrip();
    test_stack_mixed_dtypes();

    // Error cases
    test_error_null_args();
    test_error_bad_dims();
    test_error_bad_file();
    test_error_bad_format();

    // Edge cases
    test_1x1_image();
    test_large_image();

    printf("────────────────────────────────────────────────────────────\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
