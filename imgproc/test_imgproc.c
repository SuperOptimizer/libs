#define IP_IMPLEMENTATION
#include "imgproc.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

// ── Test Harness ───────────────────────────────────────────────────────────

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b)    ASSERT((a) == (b))
#define ASSERT_OK(s)       ASSERT((s) == IP_OK)
#define ASSERT_NEAR(a,b,e) ASSERT(fabs((double)(a) - (double)(b)) < (e))

// ── Tests ──────────────────────────────────────────────────────────────────

TEST(test_create_free) {
    ip_image* img = NULL;
    ASSERT_OK(ip_create(10, 20, 3, IP_U8, &img));
    ASSERT(img != NULL);
    ASSERT_EQ(img->width, 10);
    ASSERT_EQ(img->height, 20);
    ASSERT_EQ(img->channels, 3);
    ASSERT_EQ(img->dtype, IP_U8);
    ASSERT(img->flags & IP_FLAG_OWNS_DATA);
    ip_free(img);

    // Error cases
    ASSERT_EQ(ip_create(0, 10, 1, IP_U8, &img), IP_ERR_INVALID);
    ASSERT_EQ(ip_create(10, 10, 1, IP_U8, NULL), IP_ERR_NULL_ARG);
}

TEST(test_from_data) {
    uint8_t buf[12] = {0};
    ip_image* img = NULL;
    ASSERT_OK(ip_from_data(buf, 2, 2, 3, IP_U8, &img));
    ASSERT(img != NULL);
    ASSERT(!(img->flags & IP_FLAG_OWNS_DATA));
    ip_free(img);
}

TEST(test_clone) {
    ip_image* src = NULL;
    ASSERT_OK(ip_create(4, 4, 1, IP_U8, &src));
    ip_set_u8(src, 1, 2, 0, 42);

    ip_image* dst = NULL;
    ASSERT_OK(ip_clone(&dst, src));
    ASSERT_EQ(ip_get_u8(dst, 1, 2, 0), 42);
    ASSERT(dst->flags & IP_FLAG_OWNS_DATA);
    ASSERT(dst->data != src->data);

    ip_free(src);
    ip_free(dst);
}

TEST(test_pixel_get_set_u8) {
    ip_image* img = NULL;
    ASSERT_OK(ip_create(4, 4, 3, IP_U8, &img));

    ip_set_u8(img, 2, 3, 0, 100);
    ip_set_u8(img, 2, 3, 1, 150);
    ip_set_u8(img, 2, 3, 2, 200);

    ASSERT_EQ(ip_get_u8(img, 2, 3, 0), 100);
    ASSERT_EQ(ip_get_u8(img, 2, 3, 1), 150);
    ASSERT_EQ(ip_get_u8(img, 2, 3, 2), 200);

    // Other pixels should be zero (calloc)
    ASSERT_EQ(ip_get_u8(img, 0, 0, 0), 0);

    ip_free(img);
}

TEST(test_pixel_get_set_f32) {
    ip_image* img = NULL;
    ASSERT_OK(ip_create(4, 4, 1, IP_F32, &img));

    ip_set_f32(img, 1, 1, 0, 3.14f);
    ASSERT_NEAR(ip_get_f32(img, 1, 1, 0), 3.14f, 1e-5);

    ip_free(img);
}

TEST(test_resize_nearest) {
    ip_image* src = NULL;
    ASSERT_OK(ip_create(2, 2, 1, IP_U8, &src));
    ip_set_u8(src, 0, 0, 0, 10);
    ip_set_u8(src, 1, 0, 0, 20);
    ip_set_u8(src, 0, 1, 0, 30);
    ip_set_u8(src, 1, 1, 0, 40);

    ip_image* dst = NULL;
    ASSERT_OK(ip_resize(&dst, src, 4, 4, IP_NEAREST));
    ASSERT_EQ(dst->width, 4);
    ASSERT_EQ(dst->height, 4);

    // Each source pixel should map to a 2x2 block
    ASSERT_EQ(ip_get_u8(dst, 0, 0, 0), 10);
    ASSERT_EQ(ip_get_u8(dst, 1, 0, 0), 10);
    ASSERT_EQ(ip_get_u8(dst, 2, 0, 0), 20);
    ASSERT_EQ(ip_get_u8(dst, 3, 0, 0), 20);

    ip_free(src);
    ip_free(dst);
}

TEST(test_resize_bilinear) {
    ip_image* src = NULL;
    ASSERT_OK(ip_create(2, 2, 1, IP_F32, &src));
    ip_set_f32(src, 0, 0, 0, 0.0f);
    ip_set_f32(src, 1, 0, 0, 100.0f);
    ip_set_f32(src, 0, 1, 0, 0.0f);
    ip_set_f32(src, 1, 1, 0, 100.0f);

    ip_image* dst = NULL;
    ASSERT_OK(ip_resize(&dst, src, 4, 4, IP_BILINEAR));
    ASSERT_EQ(dst->width, 4);
    ASSERT_EQ(dst->height, 4);

    // Values should be interpolated - left side should be darker, right side brighter
    float left_val  = ip_get_f32(dst, 0, 0, 0);
    float right_val = ip_get_f32(dst, 3, 0, 0);
    ASSERT(right_val > left_val);

    ip_free(src);
    ip_free(dst);
}

TEST(test_cvt_rgb2gray) {
    ip_image* src = NULL;
    ASSERT_OK(ip_create(4, 4, 3, IP_U8, &src));
    // Set all pixels to pure white
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            ip_set_u8(src, x, y, 0, 255);
            ip_set_u8(src, x, y, 1, 255);
            ip_set_u8(src, x, y, 2, 255);
        }

    ip_image* gray = NULL;
    ASSERT_OK(ip_cvt_color(&gray, src, IP_RGB2GRAY));
    ASSERT_EQ(gray->channels, 1);
    // White -> gray should be ~255
    ASSERT_EQ(ip_get_u8(gray, 0, 0, 0), 255);

    ip_free(src);
    ip_free(gray);
}

TEST(test_cvt_gray2rgb) {
    ip_image* src = NULL;
    ASSERT_OK(ip_create(4, 4, 1, IP_U8, &src));
    ip_set_u8(src, 1, 1, 0, 128);

    ip_image* rgb = NULL;
    ASSERT_OK(ip_cvt_color(&rgb, src, IP_GRAY2RGB));
    ASSERT_EQ(rgb->channels, 3);
    ASSERT_EQ(ip_get_u8(rgb, 1, 1, 0), 128);
    ASSERT_EQ(ip_get_u8(rgb, 1, 1, 1), 128);
    ASSERT_EQ(ip_get_u8(rgb, 1, 1, 2), 128);

    ip_free(src);
    ip_free(rgb);
}

TEST(test_gaussian_blur_constant) {
    // Blurring a constant image should yield the same constant
    ip_image* src = NULL;
    ASSERT_OK(ip_create(10, 10, 1, IP_F32, &src));
    ASSERT_OK(ip_fill(src, 42.0));

    ip_image* dst = NULL;
    ASSERT_OK(ip_gaussian_blur(&dst, src, 3, 1.0));

    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++)
            ASSERT_NEAR(ip_get_f32(dst, x, y, 0), 42.0f, 0.01);

    ip_free(src);
    ip_free(dst);
}

TEST(test_dilate_erode) {
    ip_image* src = NULL;
    ASSERT_OK(ip_create(5, 5, 1, IP_U8, &src));
    // Single bright pixel in center
    ip_set_u8(src, 2, 2, 0, 255);

    // Dilate should spread the bright pixel
    ip_image* dilated = NULL;
    ASSERT_OK(ip_dilate(&dilated, src, 3));
    ASSERT_EQ(ip_get_u8(dilated, 2, 2, 0), 255);
    ASSERT_EQ(ip_get_u8(dilated, 1, 2, 0), 255); // neighbor
    ASSERT_EQ(ip_get_u8(dilated, 3, 2, 0), 255); // neighbor
    ASSERT_EQ(ip_get_u8(dilated, 0, 0, 0), 0);   // far away stays 0

    // Erode the dilated image: center should still be 255
    ip_image* eroded = NULL;
    ASSERT_OK(ip_erode(&eroded, dilated, 3));
    ASSERT_EQ(ip_get_u8(eroded, 2, 2, 0), 255);
    // Edges of the dilated region should be eroded back to 0
    ASSERT_EQ(ip_get_u8(eroded, 0, 0, 0), 0);

    ip_free(src);
    ip_free(dilated);
    ip_free(eroded);
}

TEST(test_threshold_binary) {
    ip_image* src = NULL;
    ASSERT_OK(ip_create(4, 4, 1, IP_U8, &src));
    ip_set_u8(src, 0, 0, 0, 50);
    ip_set_u8(src, 1, 0, 0, 150);
    ip_set_u8(src, 2, 0, 0, 200);
    ip_set_u8(src, 3, 0, 0, 100);

    ip_image* dst = NULL;
    ASSERT_OK(ip_threshold(&dst, src, 100, 255, IP_THRESH_BINARY));

    ASSERT_EQ(ip_get_u8(dst, 0, 0, 0), 0);     // 50 <= 100
    ASSERT_EQ(ip_get_u8(dst, 1, 0, 0), 255);   // 150 > 100
    ASSERT_EQ(ip_get_u8(dst, 2, 0, 0), 255);   // 200 > 100
    ASSERT_EQ(ip_get_u8(dst, 3, 0, 0), 0);     // 100 <= 100

    ip_free(src);
    ip_free(dst);
}

TEST(test_threshold_otsu) {
    ip_image* src = NULL;
    ASSERT_OK(ip_create(10, 10, 1, IP_U8, &src));
    // Create bimodal distribution: half 0, half 255
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++)
            ip_set_u8(src, x, y, 0, (x < 5) ? 0 : 255);

    ip_image* dst = NULL;
    ASSERT_OK(ip_threshold(&dst, src, 0, 255, IP_THRESH_OTSU));

    // Otsu should find a threshold between 0 and 255
    // Left side should be 0, right side should be 255
    ASSERT_EQ(ip_get_u8(dst, 0, 0, 0), 0);
    ASSERT_EQ(ip_get_u8(dst, 9, 0, 0), 255);

    ip_free(src);
    ip_free(dst);
}

TEST(test_connected_components) {
    // Create a 6x6 binary image with two distinct regions
    ip_image* bin = NULL;
    ASSERT_OK(ip_create(6, 6, 1, IP_U8, &bin));

    // Region 1: top-left 2x2 block
    ip_set_u8(bin, 0, 0, 0, 255);
    ip_set_u8(bin, 1, 0, 0, 255);
    ip_set_u8(bin, 0, 1, 0, 255);
    ip_set_u8(bin, 1, 1, 0, 255);

    // Region 2: bottom-right 2x2 block (separated by gap)
    ip_set_u8(bin, 4, 4, 0, 255);
    ip_set_u8(bin, 5, 4, 0, 255);
    ip_set_u8(bin, 4, 5, 0, 255);
    ip_set_u8(bin, 5, 5, 0, 255);

    ip_image* labels = NULL;
    int count = 0;
    ASSERT_OK(ip_connected_components(bin, &labels, &count));

    ASSERT_EQ(count, 2);
    ASSERT_EQ(labels->dtype, IP_I32);

    // Background should be 0
    ASSERT_EQ(ip_get_i32(labels, 3, 3, 0), 0);

    // Region 1 pixels should all have the same label
    int32_t l1 = ip_get_i32(labels, 0, 0, 0);
    ASSERT(l1 > 0);
    ASSERT_EQ(ip_get_i32(labels, 1, 0, 0), l1);
    ASSERT_EQ(ip_get_i32(labels, 0, 1, 0), l1);
    ASSERT_EQ(ip_get_i32(labels, 1, 1, 0), l1);

    // Region 2 pixels should all have the same label, different from region 1
    int32_t l2 = ip_get_i32(labels, 4, 4, 0);
    ASSERT(l2 > 0);
    ASSERT(l2 != l1);
    ASSERT_EQ(ip_get_i32(labels, 5, 4, 0), l2);
    ASSERT_EQ(ip_get_i32(labels, 4, 5, 0), l2);
    ASSERT_EQ(ip_get_i32(labels, 5, 5, 0), l2);

    ip_free(bin);
    ip_free(labels);
}

TEST(test_flip) {
    ip_image* src = NULL;
    ASSERT_OK(ip_create(3, 3, 1, IP_U8, &src));
    ip_set_u8(src, 0, 0, 0, 1);
    ip_set_u8(src, 2, 0, 0, 2);
    ip_set_u8(src, 0, 2, 0, 3);
    ip_set_u8(src, 2, 2, 0, 4);

    // Horizontal flip
    ip_image* hflip = NULL;
    ASSERT_OK(ip_flip(&hflip, src, 1));
    ASSERT_EQ(ip_get_u8(hflip, 0, 0, 0), 2);
    ASSERT_EQ(ip_get_u8(hflip, 2, 0, 0), 1);

    // Vertical flip
    ip_image* vflip = NULL;
    ASSERT_OK(ip_flip(&vflip, src, 0));
    ASSERT_EQ(ip_get_u8(vflip, 0, 0, 0), 3);
    ASSERT_EQ(ip_get_u8(vflip, 0, 2, 0), 1);

    // Both
    ip_image* bflip = NULL;
    ASSERT_OK(ip_flip(&bflip, src, -1));
    ASSERT_EQ(ip_get_u8(bflip, 0, 0, 0), 4);
    ASSERT_EQ(ip_get_u8(bflip, 2, 2, 0), 1);

    ip_free(src);
    ip_free(hflip);
    ip_free(vflip);
    ip_free(bflip);
}

TEST(test_rotate90) {
    // 3x2 image (w=3, h=2)
    ip_image* src = NULL;
    ASSERT_OK(ip_create(3, 2, 1, IP_U8, &src));
    // Row 0: 1 2 3
    // Row 1: 4 5 6
    ip_set_u8(src, 0, 0, 0, 1);
    ip_set_u8(src, 1, 0, 0, 2);
    ip_set_u8(src, 2, 0, 0, 3);
    ip_set_u8(src, 0, 1, 0, 4);
    ip_set_u8(src, 1, 1, 0, 5);
    ip_set_u8(src, 2, 1, 0, 6);

    // 90 CW: result is 2x3 (w=2, h=3)
    // 4 1
    // 5 2
    // 6 3
    ip_image* r90 = NULL;
    ASSERT_OK(ip_rotate90(&r90, src, 1));
    ASSERT_EQ(r90->width, 2);
    ASSERT_EQ(r90->height, 3);
    ASSERT_EQ(ip_get_u8(r90, 0, 0, 0), 4);
    ASSERT_EQ(ip_get_u8(r90, 1, 0, 0), 1);
    ASSERT_EQ(ip_get_u8(r90, 0, 1, 0), 5);
    ASSERT_EQ(ip_get_u8(r90, 1, 1, 0), 2);
    ASSERT_EQ(ip_get_u8(r90, 0, 2, 0), 6);
    ASSERT_EQ(ip_get_u8(r90, 1, 2, 0), 3);

    // 180: result is 3x2
    ip_image* r180 = NULL;
    ASSERT_OK(ip_rotate90(&r180, src, 2));
    ASSERT_EQ(r180->width, 3);
    ASSERT_EQ(r180->height, 2);
    ASSERT_EQ(ip_get_u8(r180, 0, 0, 0), 6);
    ASSERT_EQ(ip_get_u8(r180, 2, 1, 0), 1);

    ip_free(src);
    ip_free(r90);
    ip_free(r180);
}

TEST(test_min_max) {
    ip_image* img = NULL;
    ASSERT_OK(ip_create(4, 4, 1, IP_U8, &img));
    ip_set_u8(img, 0, 0, 0, 10);
    ip_set_u8(img, 1, 1, 0, 200);

    double mn, mx;
    ASSERT_OK(ip_min_max(img, &mn, &mx));
    ASSERT_NEAR(mn, 0.0, 0.01);
    ASSERT_NEAR(mx, 200.0, 0.01);

    ip_free(img);
}

TEST(test_normalize) {
    ip_image* src = NULL;
    ASSERT_OK(ip_create(4, 4, 1, IP_F32, &src));
    ip_set_f32(src, 0, 0, 0, 10.0f);
    ip_set_f32(src, 1, 0, 0, 20.0f);
    ip_set_f32(src, 2, 0, 0, 30.0f);
    ip_set_f32(src, 3, 0, 0, 40.0f);

    ip_image* dst = NULL;
    ASSERT_OK(ip_normalize(&dst, src, 0.0, 1.0));

    double mn, mx;
    ASSERT_OK(ip_min_max(dst, &mn, &mx));
    ASSERT_NEAR(mn, 0.0, 0.01);
    ASSERT_NEAR(mx, 1.0, 0.01);

    ip_free(src);
    ip_free(dst);
}

TEST(test_status_str) {
    ASSERT(strcmp(ip_status_str(IP_OK), "OK") == 0);
    ASSERT(strcmp(ip_version_str(), "0.1.0") == 0);
    ASSERT_EQ(ip_dtype_size(IP_U8), 1u);
    ASSERT_EQ(ip_dtype_size(IP_F32), 4u);
}

// ── Main ───────────────────────────────────────────────────────────────────

int main(void) {
    printf("imgproc.h test suite\n");
    printf("────────────────────────────────────────────────────────\n");

    RUN(test_create_free);
    RUN(test_from_data);
    RUN(test_clone);
    RUN(test_pixel_get_set_u8);
    RUN(test_pixel_get_set_f32);
    RUN(test_resize_nearest);
    RUN(test_resize_bilinear);
    RUN(test_cvt_rgb2gray);
    RUN(test_cvt_gray2rgb);
    RUN(test_gaussian_blur_constant);
    RUN(test_dilate_erode);
    RUN(test_threshold_binary);
    RUN(test_threshold_otsu);
    RUN(test_connected_components);
    RUN(test_flip);
    RUN(test_rotate90);
    RUN(test_min_max);
    RUN(test_normalize);
    RUN(test_status_str);

    printf("────────────────────────────────────────────────────────\n");
    printf("  %d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
