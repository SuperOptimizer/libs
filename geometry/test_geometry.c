#define GM_IMPLEMENTATION
#include "geometry.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

// -- Test Harness ------------------------------------------------------------

static int g_pass = 0, g_fail = 0;

#define ASSERT_TRUE(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while(0)

#define ASSERT_NEAR(a, b, eps, msg) ASSERT_TRUE(fabs((double)(a) - (double)(b)) < (eps), msg)
#define ASSERT_OK(s, msg) ASSERT_TRUE((s) == GM_OK, msg)

// -- Tests -------------------------------------------------------------------

static void test_version(void) {
    const char* v = gm_version_str();
    ASSERT_TRUE(strcmp(v, "0.1.0") == 0, "version string is 0.1.0");
    ASSERT_TRUE(strcmp(gm_status_str(GM_OK), "ok") == 0, "status ok string");
    ASSERT_TRUE(strcmp(gm_status_str(GM_ERR_SINGULAR), "singular matrix") == 0, "status singular string");
}

static void test_vec3f_ops(void) {
    gm_vec3f a = {1, 2, 3}, b = {4, 5, 6};

    gm_vec3f sum = gm_add3f(a, b);
    ASSERT_NEAR(sum.x, 5, 1e-6, "add3f x");
    ASSERT_NEAR(sum.y, 7, 1e-6, "add3f y");
    ASSERT_NEAR(sum.z, 9, 1e-6, "add3f z");

    gm_vec3f diff = gm_sub3f(a, b);
    ASSERT_NEAR(diff.x, -3, 1e-6, "sub3f x");

    gm_vec3f prod = gm_mul3f(a, b);
    ASSERT_NEAR(prod.x, 4, 1e-6, "mul3f x");
    ASSERT_NEAR(prod.y, 10, 1e-6, "mul3f y");

    gm_vec3f scaled = gm_scale3f(a, 2.0f);
    ASSERT_NEAR(scaled.x, 2, 1e-6, "scale3f x");
    ASSERT_NEAR(scaled.z, 6, 1e-6, "scale3f z");

    float d = gm_dot3f(a, b);
    ASSERT_NEAR(d, 32, 1e-6, "dot3f");

    gm_vec3f c = gm_cross3f(a, b);
    ASSERT_NEAR(c.x, -3, 1e-6, "cross3f x");
    ASSERT_NEAR(c.y, 6, 1e-6, "cross3f y");
    ASSERT_NEAR(c.z, -3, 1e-6, "cross3f z");

    float len = gm_len3f(a);
    ASSERT_NEAR(len, sqrtf(14.0f), 1e-5, "len3f");

    gm_vec3f n = gm_normalize3f(a);
    ASSERT_NEAR(gm_len3f(n), 1.0f, 1e-5, "normalize3f unit length");

    float dist = gm_dist3f(a, b);
    ASSERT_NEAR(dist, sqrtf(27.0f), 1e-5, "dist3f");

    gm_vec3f mid = gm_lerp3f(a, b, 0.5f);
    ASSERT_NEAR(mid.x, 2.5f, 1e-6, "lerp3f x");
    ASSERT_NEAR(mid.y, 3.5f, 1e-6, "lerp3f y");
}

static void test_vec3d_ops(void) {
    gm_vec3d a = {1, 0, 0}, b = {0, 1, 0};
    gm_vec3d c = gm_cross3d(a, b);
    ASSERT_NEAR(c.z, 1.0, 1e-12, "cross3d z-axis");
    ASSERT_NEAR(gm_dot3d(a, b), 0.0, 1e-12, "dot3d orthogonal");
    ASSERT_NEAR(gm_len3d(a), 1.0, 1e-12, "len3d unit");
    ASSERT_NEAR(gm_dist3d(a, b), sqrt(2.0), 1e-12, "dist3d");
}

static void test_vec2f_ops(void) {
    gm_vec2f a = {3, 4};
    ASSERT_NEAR(gm_len2f(a), 5.0f, 1e-5, "len2f");
    gm_vec2f n = gm_normalize2f(a);
    ASSERT_NEAR(n.x, 0.6f, 1e-5, "normalize2f x");
    ASSERT_NEAR(n.y, 0.8f, 1e-5, "normalize2f y");
}

static void test_mat3_multiply(void) {
    gm_mat3 I = gm_mat3_identity();
    gm_mat3 A = {{ 1,2,3, 4,5,6, 7,8,9 }};
    gm_mat3 R = gm_mat3_mul(I, A);
    for (int i = 0; i < 9; i++)
        ASSERT_NEAR(R.m[i], A.m[i], 1e-12, "mat3_mul identity");

    // Transpose
    gm_mat3 T = gm_mat3_transpose(A);
    ASSERT_NEAR(T.m[1], 4, 1e-12, "mat3_transpose [0][1]");
    ASSERT_NEAR(T.m[3], 2, 1e-12, "mat3_transpose [1][0]");
}

static void test_mat3_det_inv(void) {
    gm_mat3 A = {{ 1,2,3, 0,1,4, 5,6,0 }};
    double det = gm_mat3_det(A);
    ASSERT_NEAR(det, 1, 1e-12, "mat3_det");

    gm_mat3 inv;
    gm_status s = gm_mat3_inv(A, &inv);
    ASSERT_OK(s, "mat3_inv ok");

    // A * inv should be identity
    gm_mat3 I = gm_mat3_mul(A, inv);
    ASSERT_NEAR(I.m[0], 1, 1e-9, "mat3 A*inv [0][0]");
    ASSERT_NEAR(I.m[4], 1, 1e-9, "mat3 A*inv [1][1]");
    ASSERT_NEAR(I.m[8], 1, 1e-9, "mat3 A*inv [2][2]");
    ASSERT_NEAR(I.m[1], 0, 1e-9, "mat3 A*inv [0][1]");
    ASSERT_NEAR(I.m[3], 0, 1e-9, "mat3 A*inv [1][0]");

    // Singular matrix
    gm_mat3 S = {{ 1,2,3, 4,5,6, 7,8,9 }};
    s = gm_mat3_inv(S, &inv);
    ASSERT_TRUE(s == GM_ERR_SINGULAR, "mat3_inv singular");
}

static void test_mat4_multiply_inv(void) {
    gm_mat4 T = gm_translation(1, 2, 3);
    gm_mat4 I = gm_mat4_identity();
    gm_mat4 R = gm_mat4_mul(T, I);
    ASSERT_NEAR(R.m[3], 1, 1e-12, "mat4 translation tx");
    ASSERT_NEAR(R.m[7], 2, 1e-12, "mat4 translation ty");
    ASSERT_NEAR(R.m[11], 3, 1e-12, "mat4 translation tz");

    gm_mat4 inv;
    gm_status s = gm_mat4_inv(T, &inv);
    ASSERT_OK(s, "mat4_inv ok");
    gm_mat4 prod = gm_mat4_mul(T, inv);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            ASSERT_NEAR(prod.m[i*4+j], (i==j) ? 1.0 : 0.0, 1e-9, "mat4 T*inv identity");

    // mat4_mulv
    gm_vec4f v = {1, 2, 3, 1};
    gm_vec4f tv = gm_mat4_mulv(T, v);
    ASSERT_NEAR(tv.x, 2, 1e-6, "mat4_mulv tx");
    ASSERT_NEAR(tv.y, 4, 1e-6, "mat4_mulv ty");
    ASSERT_NEAR(tv.z, 6, 1e-6, "mat4_mulv tz");
    ASSERT_NEAR(tv.w, 1, 1e-6, "mat4_mulv tw");

    // Transpose
    gm_mat4 Tt = gm_mat4_transpose(T);
    ASSERT_NEAR(Tt.m[12], 1, 1e-12, "mat4_transpose [3][0]");
}

static void test_rodrigues_roundtrip(void) {
    // Axis-angle -> matrix -> axis-angle
    gm_vec3d rvec = {0.1, 0.2, 0.3};
    gm_mat3 R;
    gm_status s = gm_rodrigues(&rvec, &R);
    ASSERT_OK(s, "rodrigues forward");

    // Check it's a rotation: R^T * R = I
    gm_mat3 RtR = gm_mat3_mul(gm_mat3_transpose(R), R);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            ASSERT_NEAR(RtR.m[i*3+j], (i==j) ? 1.0 : 0.0, 1e-9, "rodrigues R^T*R = I");

    // det(R) = 1
    ASSERT_NEAR(gm_mat3_det(R), 1.0, 1e-9, "rodrigues det=1");

    // Inverse
    gm_vec3d rvec2;
    s = gm_rodrigues_inv(&R, &rvec2);
    ASSERT_OK(s, "rodrigues_inv");
    ASSERT_NEAR(rvec2.x, rvec.x, 1e-9, "rodrigues roundtrip x");
    ASSERT_NEAR(rvec2.y, rvec.y, 1e-9, "rodrigues roundtrip y");
    ASSERT_NEAR(rvec2.z, rvec.z, 1e-9, "rodrigues roundtrip z");

    // Zero rotation
    gm_vec3d zero = {0, 0, 0};
    s = gm_rodrigues(&zero, &R);
    ASSERT_OK(s, "rodrigues zero");
    ASSERT_NEAR(R.m[0], 1, 1e-12, "rodrigues zero -> identity");
}

static void test_rotation_matrices(void) {
    double angle = GM_PI / 4.0;
    gm_mat3 Rx = gm_rotation_x(angle);
    gm_mat3 Ry = gm_rotation_y(angle);
    gm_mat3 Rz = gm_rotation_z(angle);

    // All should be proper rotations
    ASSERT_NEAR(gm_mat3_det(Rx), 1.0, 1e-9, "Rx det=1");
    ASSERT_NEAR(gm_mat3_det(Ry), 1.0, 1e-9, "Ry det=1");
    ASSERT_NEAR(gm_mat3_det(Rz), 1.0, 1e-9, "Rz det=1");

    // Rx should leave x-axis unchanged
    gm_vec3d ex = {1, 0, 0};
    gm_vec3d rx = gm_mat3_mulv(Rx, ex);
    ASSERT_NEAR(rx.x, 1, 1e-12, "Rx leaves x unchanged");
    ASSERT_NEAR(rx.y, 0, 1e-12, "Rx leaves x unchanged y");
    ASSERT_NEAR(rx.z, 0, 1e-12, "Rx leaves x unchanged z");
}

static void test_look_at(void) {
    gm_vec3d eye = {0, 0, 5};
    gm_vec3d target = {0, 0, 0};
    gm_vec3d up = {0, 1, 0};
    gm_mat4 V = gm_look_at(eye, target, up);

    // Eye should map to origin
    gm_vec4f e4 = {0, 0, 5, 1};
    gm_vec4f ve = gm_mat4_mulv(V, e4);
    ASSERT_NEAR(ve.x, 0, 1e-9, "look_at eye->origin x");
    ASSERT_NEAR(ve.y, 0, 1e-9, "look_at eye->origin y");
    ASSERT_NEAR(ve.z, 0, 1e-9, "look_at eye->origin z");
}

static void test_perspective(void) {
    gm_mat4 P = gm_perspective(gm_deg2rad(90.0), 1.0, 0.1, 100.0);
    // For 90 degree fov, f = 1/tan(45) = 1, aspect=1, so P[0][0] = 1
    ASSERT_NEAR(P.m[0], 1.0, 1e-9, "perspective fov90 P00");
    ASSERT_NEAR(P.m[5], 1.0, 1e-9, "perspective fov90 P11");
    ASSERT_NEAR(P.m[14], -1.0, 1e-9, "perspective P32 = -1");
}

static void test_bbox3f(void) {
    gm_bbox3f b = gm_bbox3f_empty();
    ASSERT_TRUE(!gm_bbox3f_contains(b, (gm_vec3f){0,0,0}), "empty bbox contains nothing");

    b = gm_bbox3f_expand(b, (gm_vec3f){1, 2, 3});
    b = gm_bbox3f_expand(b, (gm_vec3f){-1, -2, -3});
    ASSERT_NEAR(b.lo.x, -1, 1e-6, "bbox expand lo.x");
    ASSERT_NEAR(b.hi.z, 3, 1e-6, "bbox expand hi.z");
    ASSERT_TRUE(gm_bbox3f_contains(b, (gm_vec3f){0,0,0}), "bbox contains origin");
    ASSERT_TRUE(!gm_bbox3f_contains(b, (gm_vec3f){5,0,0}), "bbox not contains outside");

    gm_vec3f center = gm_bbox3f_center(b);
    ASSERT_NEAR(center.x, 0, 1e-6, "bbox center x");

    gm_vec3f size = gm_bbox3f_size(b);
    ASSERT_NEAR(size.x, 2, 1e-6, "bbox size x");
    ASSERT_NEAR(size.y, 4, 1e-6, "bbox size y");

    float vol = gm_bbox3f_volume(b);
    ASSERT_NEAR(vol, 2*4*6, 1e-5, "bbox volume");

    // Intersection test
    gm_bbox3f b2 = { {0,0,0}, {5,5,5} };
    ASSERT_TRUE(gm_bbox3f_intersects(b, b2), "bbox intersects");
    gm_bbox3f b3 = { {10,10,10}, {20,20,20} };
    ASSERT_TRUE(!gm_bbox3f_intersects(b, b3), "bbox not intersects");

    // Union
    gm_bbox3f u = gm_bbox3f_union(b, b2);
    ASSERT_NEAR(u.lo.x, -1, 1e-6, "bbox union lo.x");
    ASSERT_NEAR(u.hi.x, 5, 1e-6, "bbox union hi.x");
}

static void test_quaternion_slerp(void) {
    gm_quat q0 = gm_quat_identity();
    gm_quat q1 = gm_quat_from_axis_angle((gm_vec3d){0,0,1}, GM_PI/2);

    // Slerp at t=0 -> q0
    gm_quat s0 = gm_quat_slerp(q0, q1, 0.0);
    ASSERT_NEAR(s0.w, q0.w, 1e-9, "slerp t=0 w");
    ASSERT_NEAR(s0.z, q0.z, 1e-9, "slerp t=0 z");

    // Slerp at t=1 -> q1
    gm_quat s1 = gm_quat_slerp(q0, q1, 1.0);
    ASSERT_NEAR(s1.w, q1.w, 1e-9, "slerp t=1 w");
    ASSERT_NEAR(s1.z, q1.z, 1e-9, "slerp t=1 z");

    // Slerp at t=0.5 -> halfway rotation (pi/4 around z)
    gm_quat s05 = gm_quat_slerp(q0, q1, 0.5);
    gm_quat expected = gm_quat_from_axis_angle((gm_vec3d){0,0,1}, GM_PI/4);
    ASSERT_NEAR(s05.w, expected.w, 1e-9, "slerp t=0.5 w");
    ASSERT_NEAR(s05.z, expected.z, 1e-9, "slerp t=0.5 z");

    // Quaternion rotation: rotate (1,0,0) by 90 deg around z -> (0,1,0)
    gm_vec3d v = gm_quat_rotate_vec3d(q1, (gm_vec3d){1,0,0});
    ASSERT_NEAR(v.x, 0, 1e-9, "quat rotate x");
    ASSERT_NEAR(v.y, 1, 1e-9, "quat rotate y");
    ASSERT_NEAR(v.z, 0, 1e-9, "quat rotate z");
}

static void test_quat_mat3_roundtrip(void) {
    gm_quat q = gm_quat_from_axis_angle((gm_vec3d){1,1,1}, GM_PI/3);
    q = gm_quat_normalize(q);
    gm_mat3 M = gm_quat_to_mat3(q);
    gm_quat q2 = gm_quat_from_mat3(M);

    // Quaternions are equivalent up to sign
    double dot = q.w*q2.w + q.x*q2.x + q.y*q2.y + q.z*q2.z;
    ASSERT_NEAR(fabs(dot), 1.0, 1e-9, "quat->mat3->quat roundtrip");
}

static void test_svd3x3(void) {
    gm_mat3 A = {{ 1,2,0, 0,1,1, 1,0,1 }};
    gm_mat3 U, V;
    gm_vec3d sigma;
    gm_status s = gm_svd3x3(&A, &U, &sigma, &V);
    ASSERT_OK(s, "svd3x3 ok");

    // Reconstruct: U * diag(sigma) * V^T should equal A
    gm_mat3 S = {0};
    S.m[0] = sigma.x; S.m[4] = sigma.y; S.m[8] = sigma.z;
    gm_mat3 US = gm_mat3_mul(U, S);
    gm_mat3 USVT = gm_mat3_mul(US, gm_mat3_transpose(V));

    for (int i = 0; i < 9; i++)
        ASSERT_NEAR(USVT.m[i], A.m[i], 1e-6, "svd3x3 reconstruction");

    // Singular values should be non-negative
    ASSERT_TRUE(sigma.x >= 0, "svd sigma0 >= 0");
    ASSERT_TRUE(sigma.y >= 0, "svd sigma1 >= 0");
    ASSERT_TRUE(sigma.z >= 0, "svd sigma2 >= 0");
}

static void test_affine_estimation(void) {
    // Create known affine transform: scale by 2 and translate by (1,2,3)
    gm_vec3d src[6] = {
        {0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}, {1,1,0}, {1,1,1}
    };
    gm_vec3d dst[6];
    for (int i = 0; i < 6; i++) {
        dst[i].x = 2*src[i].x + 1;
        dst[i].y = 2*src[i].y + 2;
        dst[i].z = 2*src[i].z + 3;
    }

    gm_mat4 M;
    gm_status s = gm_estimate_affine3d(src, dst, 6, &M);
    ASSERT_OK(s, "estimate_affine3d ok");

    // Check: M * [src 1]^T should give dst
    for (int i = 0; i < 6; i++) {
        gm_vec4f p = {(float)src[i].x, (float)src[i].y, (float)src[i].z, 1.0f};
        gm_vec4f r = gm_mat4_mulv(M, p);
        ASSERT_NEAR(r.x, dst[i].x, 1e-6, "affine fit x");
        ASSERT_NEAR(r.y, dst[i].y, 1e-6, "affine fit y");
        ASSERT_NEAR(r.z, dst[i].z, 1e-6, "affine fit z");
    }
}

static void test_utilities(void) {
    ASSERT_NEAR(gm_deg2rad(180.0), GM_PI, 1e-12, "deg2rad 180");
    ASSERT_NEAR(gm_rad2deg(GM_PI), 180.0, 1e-9, "rad2deg pi");
    ASSERT_NEAR(gm_clampf(-1.0f, 0.0f, 1.0f), 0.0f, 1e-6, "clampf lo");
    ASSERT_NEAR(gm_clampf(2.0f, 0.0f, 1.0f), 1.0f, 1e-6, "clampf hi");
    ASSERT_NEAR(gm_clampd(0.5, 0.0, 1.0), 0.5, 1e-12, "clampd mid");
}

// -- Main --------------------------------------------------------------------

int main(void) {
    printf("geometry.h test suite\n");
    printf("version: %s\n\n", gm_version_str());

    test_version();
    test_utilities();
    test_vec2f_ops();
    test_vec3f_ops();
    test_vec3d_ops();
    test_mat3_multiply();
    test_mat3_det_inv();
    test_mat4_multiply_inv();
    test_rodrigues_roundtrip();
    test_rotation_matrices();
    test_look_at();
    test_perspective();
    test_bbox3f();
    test_quaternion_slerp();
    test_quat_mat3_roundtrip();
    test_svd3x3();
    test_affine_estimation();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
