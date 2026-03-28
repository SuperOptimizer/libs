#define TS_IMPLEMENTATION
#include "tensor.h"

#include <stdio.h>
#include <math.h>
#include <assert.h>

#define ASSERT_OK(expr) do { \
    ts_status _s = (expr); \
    if (_s != TS_OK) { \
        fprintf(stderr, "FAIL %s:%d: %s returned %s\n", \
                __FILE__, __LINE__, #expr, ts_status_str(_s)); \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ_F64(a, b) do { \
    double _a = (a), _b = (b); \
    if (fabs(_a - _b) > 1e-9) { \
        fprintf(stderr, "FAIL %s:%d: %.9g != %.9g\n", \
                __FILE__, __LINE__, _a, _b); \
        return 1; \
    } \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while(0)

#define RUN_TEST(fn) do { \
    printf("  %-40s", #fn); \
    if (fn() == 0) printf("OK\n"); \
    else { printf("FAILED\n"); failures++; } \
} while(0)

// ── Tests ───────────────────────────────────────────────────────────────────

static int test_version(void)
{
    const char* v = ts_version_str();
    ASSERT_TRUE(v != NULL);
    ASSERT_TRUE(v[0] == '0');
    return 0;
}

static int test_dtype(void)
{
    ASSERT_TRUE(ts_dtype_size(TS_U8)  == 1);
    ASSERT_TRUE(ts_dtype_size(TS_F32) == 4);
    ASSERT_TRUE(ts_dtype_size(TS_F64) == 8);
    ASSERT_TRUE(ts_dtype_size(TS_I64) == 8);
    return 0;
}

static int test_zeros(void)
{
    ts_tensor* t;
    int64_t shape[] = {3, 4};
    ASSERT_OK(ts_zeros(&t, 2, shape, TS_F32));

    ASSERT_TRUE(t->ndim == 2);
    ASSERT_TRUE(t->shape[0] == 3);
    ASSERT_TRUE(t->shape[1] == 4);
    ASSERT_TRUE(ts_nelem(t) == 12);
    ASSERT_TRUE(ts_is_contiguous(t));

    int64_t idx[] = {1, 2};
    ASSERT_EQ_F64(ts_get_f32(t, idx), 0.0);

    ts_free(t);
    return 0;
}

static int test_ones(void)
{
    ts_tensor* t;
    int64_t shape[] = {2, 3};
    ASSERT_OK(ts_ones(&t, 2, shape, TS_F64));

    for (int64_t i = 0; i < 2; i++)
        for (int64_t j = 0; j < 3; j++) {
            int64_t idx[] = {i, j};
            ASSERT_EQ_F64(ts_get_f64(t, idx), 1.0);
        }

    ts_free(t);
    return 0;
}

static int test_full(void)
{
    ts_tensor* t;
    int64_t shape[] = {5};
    int32_t val = 42;
    ASSERT_OK(ts_full(&t, 1, shape, TS_I32, &val));

    for (int64_t i = 0; i < 5; i++) {
        int64_t idx[] = {i};
        ASSERT_TRUE(ts_get_i32(t, idx) == 42);
    }

    ts_free(t);
    return 0;
}

static int test_from_data(void)
{
    float data[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    ts_tensor* t;
    int64_t shape[] = {2, 3};
    ASSERT_OK(ts_from_data(&t, data, 2, shape, TS_F32));

    ASSERT_TRUE(!(t->flags & TS_FLAG_OWNS_DATA));
    int64_t idx[] = {1, 0};
    ASSERT_EQ_F64(ts_get_f32(t, idx), 4.0);

    ts_free(t); // should NOT free data
    ASSERT_EQ_F64(data[3], 4.0); // data still intact
    return 0;
}

static int test_clone(void)
{
    ts_tensor* a;
    int64_t shape[] = {2, 2};
    ASSERT_OK(ts_zeros(&a, 2, shape, TS_F32));
    int64_t idx[] = {0, 1};
    ts_set_f32(a, idx, 3.14f);

    ts_tensor* b;
    ASSERT_OK(ts_clone(&b, a));
    ASSERT_EQ_F64(ts_get_f32(b, idx), 3.14f);

    // Modifying clone doesn't affect original
    ts_set_f32(b, idx, 0.0f);
    ASSERT_EQ_F64(ts_get_f32(a, idx), 3.14f);

    ts_free(a);
    ts_free(b);
    return 0;
}

static int test_set_get(void)
{
    ts_tensor* t;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&t, 1, shape, TS_U8));

    int64_t i0[] = {0}, i1[] = {1}, i2[] = {2};
    ts_set_u8(t, i0, 10);
    ts_set_u8(t, i1, 20);
    ts_set_u8(t, i2, 255);

    ASSERT_TRUE(ts_get_u8(t, i0) == 10);
    ASSERT_TRUE(ts_get_u8(t, i1) == 20);
    ASSERT_TRUE(ts_get_u8(t, i2) == 255);

    ts_free(t);
    return 0;
}

static int test_view_slice(void)
{
    // Create a 4x4 matrix
    ts_tensor* t;
    int64_t shape[] = {4, 4};
    ASSERT_OK(ts_zeros(&t, 2, shape, TS_F32));

    // Fill with row*10 + col
    for (int64_t r = 0; r < 4; r++)
        for (int64_t c = 0; c < 4; c++) {
            int64_t idx[] = {r, c};
            ts_set_f32(t, idx, (float)(r * 10 + c));
        }

    // Slice [1:3, 1:3] — should be a 2x2 submatrix
    ts_slice slices[] = {{1, 3, 1}, {1, 3, 1}};
    ts_tensor* v;
    ASSERT_OK(ts_view(&v, t, slices));

    ASSERT_TRUE(v->shape[0] == 2);
    ASSERT_TRUE(v->shape[1] == 2);
    ASSERT_TRUE(!(v->flags & TS_FLAG_OWNS_DATA));

    int64_t idx00[] = {0, 0};
    int64_t idx01[] = {0, 1};
    int64_t idx10[] = {1, 0};
    int64_t idx11[] = {1, 1};
    ASSERT_EQ_F64(ts_get_f32(v, idx00), 11.0);
    ASSERT_EQ_F64(ts_get_f32(v, idx01), 12.0);
    ASSERT_EQ_F64(ts_get_f32(v, idx10), 21.0);
    ASSERT_EQ_F64(ts_get_f32(v, idx11), 22.0);

    // Writing through view modifies original
    ts_set_f32(v, idx00, 99.0f);
    int64_t orig_idx[] = {1, 1};
    ASSERT_EQ_F64(ts_get_f32(t, orig_idx), 99.0);

    ts_free(v);
    ts_free(t);
    return 0;
}

static int test_view_step(void)
{
    // 1D tensor [0,1,2,3,4,5,6,7]
    ts_tensor* t;
    int64_t shape[] = {8};
    ASSERT_OK(ts_zeros(&t, 1, shape, TS_I32));
    for (int64_t i = 0; i < 8; i++) {
        int64_t idx[] = {i};
        ts_set_i32(t, idx, (int32_t)i);
    }

    // Slice [::2] -> [0,2,4,6]
    ts_slice s[] = {{0, INT64_MAX, 2}};
    ts_tensor* v;
    ASSERT_OK(ts_view(&v, t, s));

    ASSERT_TRUE(v->shape[0] == 4);
    int64_t i0[] = {0}, i1[] = {1}, i2[] = {2}, i3[] = {3};
    ASSERT_TRUE(ts_get_i32(v, i0) == 0);
    ASSERT_TRUE(ts_get_i32(v, i1) == 2);
    ASSERT_TRUE(ts_get_i32(v, i2) == 4);
    ASSERT_TRUE(ts_get_i32(v, i3) == 6);

    ts_free(v);
    ts_free(t);
    return 0;
}

static int test_reshape(void)
{
    ts_tensor* t;
    int64_t shape[] = {2, 3};
    ASSERT_OK(ts_zeros(&t, 2, shape, TS_F32));
    for (int64_t i = 0; i < 6; i++) {
        float* p = (float*)t->data + i;
        *p = (float)i;
    }

    ts_tensor* r;
    int64_t new_shape[] = {3, 2};
    ASSERT_OK(ts_reshape(&r, t, 2, new_shape));

    ASSERT_TRUE(r->shape[0] == 3);
    ASSERT_TRUE(r->shape[1] == 2);

    int64_t idx[] = {1, 0};
    ASSERT_EQ_F64(ts_get_f32(r, idx), 2.0);

    ts_free(r);
    ts_free(t);
    return 0;
}

static int test_transpose(void)
{
    ts_tensor* t;
    int64_t shape[] = {2, 3};
    ASSERT_OK(ts_zeros(&t, 2, shape, TS_F32));

    // Fill: t[r][c] = r*10 + c
    for (int64_t r = 0; r < 2; r++)
        for (int64_t c = 0; c < 3; c++) {
            int64_t idx[] = {r, c};
            ts_set_f32(t, idx, (float)(r * 10 + c));
        }

    ts_tensor* tr;
    ASSERT_OK(ts_transpose(&tr, t, NULL));

    ASSERT_TRUE(tr->shape[0] == 3);
    ASSERT_TRUE(tr->shape[1] == 2);

    int64_t idx[] = {2, 1};
    ASSERT_EQ_F64(ts_get_f32(tr, idx), 12.0); // original [1][2]

    ts_free(tr);
    ts_free(t);
    return 0;
}

static int test_squeeze_unsqueeze(void)
{
    ts_tensor* t;
    int64_t shape[] = {1, 3, 1};
    ASSERT_OK(ts_zeros(&t, 3, shape, TS_F32));

    ts_tensor* s;
    ASSERT_OK(ts_squeeze(&s, t, 0));
    ASSERT_TRUE(s->ndim == 2);
    ASSERT_TRUE(s->shape[0] == 3);
    ASSERT_TRUE(s->shape[1] == 1);

    ts_tensor* u;
    ASSERT_OK(ts_unsqueeze(&u, s, 0));
    ASSERT_TRUE(u->ndim == 3);
    ASSERT_TRUE(u->shape[0] == 1);
    ASSERT_TRUE(u->shape[1] == 3);

    ts_free(u);
    ts_free(s);
    ts_free(t);
    return 0;
}

static int test_add(void)
{
    ts_tensor *a, *b, *c;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    ASSERT_OK(ts_zeros(&b, 1, shape, TS_F32));

    int64_t i0[] = {0}, i1[] = {1}, i2[] = {2};
    ts_set_f32(a, i0, 1.0f); ts_set_f32(a, i1, 2.0f); ts_set_f32(a, i2, 3.0f);
    ts_set_f32(b, i0, 10.0f); ts_set_f32(b, i1, 20.0f); ts_set_f32(b, i2, 30.0f);

    ASSERT_OK(ts_add(&c, a, b));
    ASSERT_EQ_F64(ts_get_f32(c, i0), 11.0);
    ASSERT_EQ_F64(ts_get_f32(c, i1), 22.0);
    ASSERT_EQ_F64(ts_get_f32(c, i2), 33.0);

    ts_free(a); ts_free(b); ts_free(c);
    return 0;
}

static int test_broadcast(void)
{
    // a: [3, 1], b: [1, 4] -> out: [3, 4]
    ts_tensor *a, *b, *c;
    int64_t sa[] = {3, 1}, sb[] = {1, 4};
    ASSERT_OK(ts_zeros(&a, 2, sa, TS_F32));
    ASSERT_OK(ts_zeros(&b, 2, sb, TS_F32));

    // a = [[1],[2],[3]]
    int64_t a0[] = {0,0}, a1[] = {1,0}, a2[] = {2,0};
    ts_set_f32(a, a0, 1.0f);
    ts_set_f32(a, a1, 2.0f);
    ts_set_f32(a, a2, 3.0f);

    // b = [[10,20,30,40]]
    int64_t b0[] = {0,0}, b1[] = {0,1}, b2[] = {0,2}, b3[] = {0,3};
    ts_set_f32(b, b0, 10.0f);
    ts_set_f32(b, b1, 20.0f);
    ts_set_f32(b, b2, 30.0f);
    ts_set_f32(b, b3, 40.0f);

    ASSERT_OK(ts_add(&c, a, b));
    ASSERT_TRUE(c->shape[0] == 3);
    ASSERT_TRUE(c->shape[1] == 4);

    int64_t idx[] = {2, 3};
    ASSERT_EQ_F64(ts_get_f32(c, idx), 43.0); // 3 + 40

    ts_free(a); ts_free(b); ts_free(c);
    return 0;
}

static int test_scale(void)
{
    ts_tensor *a, *b;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    int64_t i0[] = {0}, i1[] = {1}, i2[] = {2};
    ts_set_f32(a, i0, 1.0f); ts_set_f32(a, i1, 2.0f); ts_set_f32(a, i2, 3.0f);

    ASSERT_OK(ts_scale(&b, a, 0.5));
    ASSERT_EQ_F64(ts_get_f32(b, i0), 0.5);
    ASSERT_EQ_F64(ts_get_f32(b, i1), 1.0);
    ASSERT_EQ_F64(ts_get_f32(b, i2), 1.5);

    ts_free(a); ts_free(b);
    return 0;
}

static int test_cast(void)
{
    ts_tensor *a, *b;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    int64_t i0[] = {0}, i1[] = {1}, i2[] = {2};
    ts_set_f32(a, i0, 1.5f); ts_set_f32(a, i1, 2.7f); ts_set_f32(a, i2, 255.0f);

    ASSERT_OK(ts_cast(&b, a, TS_U8));
    ASSERT_TRUE(ts_get_u8(b, i0) == 1);
    ASSERT_TRUE(ts_get_u8(b, i1) == 2);
    ASSERT_TRUE(ts_get_u8(b, i2) == 255);

    ts_free(a); ts_free(b);
    return 0;
}

static int test_sum(void)
{
    ts_tensor *t, *s;
    int64_t shape[] = {2, 3};
    ASSERT_OK(ts_zeros(&t, 2, shape, TS_F32));

    // [[1,2,3],[4,5,6]]
    float vals[] = {1,2,3,4,5,6};
    for (int i = 0; i < 6; i++)
        ((float*)t->data)[i] = vals[i];

    // Global sum
    ASSERT_OK(ts_sum(&s, t, -1));
    ASSERT_EQ_F64(ts_get_f64(s, (int64_t[]){0}), 21.0);
    ts_free(s);

    // Sum along axis 0 -> [5, 7, 9]
    ASSERT_OK(ts_sum(&s, t, 0));
    ASSERT_TRUE(s->shape[0] == 1);
    ASSERT_TRUE(s->shape[1] == 3);
    ASSERT_EQ_F64(ts_get_f32(s, (int64_t[]){0, 0}), 5.0);
    ASSERT_EQ_F64(ts_get_f32(s, (int64_t[]){0, 1}), 7.0);
    ASSERT_EQ_F64(ts_get_f32(s, (int64_t[]){0, 2}), 9.0);
    ts_free(s);

    // Sum along axis 1 -> [[6],[15]]
    ASSERT_OK(ts_sum(&s, t, 1));
    ASSERT_TRUE(s->shape[0] == 2);
    ASSERT_TRUE(s->shape[1] == 1);
    ASSERT_EQ_F64(ts_get_f32(s, (int64_t[]){0, 0}), 6.0);
    ASSERT_EQ_F64(ts_get_f32(s, (int64_t[]){1, 0}), 15.0);
    ts_free(s);

    ts_free(t);
    return 0;
}

static int test_mean(void)
{
    ts_tensor *t, *m;
    int64_t shape[] = {4};
    ASSERT_OK(ts_zeros(&t, 1, shape, TS_F32));
    ((float*)t->data)[0] = 1.0f;
    ((float*)t->data)[1] = 2.0f;
    ((float*)t->data)[2] = 3.0f;
    ((float*)t->data)[3] = 4.0f;

    ASSERT_OK(ts_mean(&m, t, -1));
    ASSERT_EQ_F64(ts_get_f64(m, (int64_t[]){0}), 2.5);

    ts_free(m);
    ts_free(t);
    return 0;
}

static int test_min_max(void)
{
    ts_tensor *t, *mn, *mx;
    int64_t shape[] = {5};
    ASSERT_OK(ts_zeros(&t, 1, shape, TS_F32));
    float vals[] = {3, 1, 4, 1, 5};
    for (int i = 0; i < 5; i++)
        ((float*)t->data)[i] = vals[i];

    ASSERT_OK(ts_min(&mn, t, -1));
    ASSERT_OK(ts_max(&mx, t, -1));

    ASSERT_EQ_F64(ts_get_f32(mn, (int64_t[]){0}), 1.0);
    ASSERT_EQ_F64(ts_get_f32(mx, (int64_t[]){0}), 5.0);

    ts_free(mn); ts_free(mx); ts_free(t);
    return 0;
}

static int test_contiguous(void)
{
    // Create a 4x4, slice rows [::2] to get non-contiguous
    ts_tensor* t;
    int64_t shape[] = {4, 4};
    ASSERT_OK(ts_zeros(&t, 2, shape, TS_F32));
    for (int i = 0; i < 16; i++)
        ((float*)t->data)[i] = (float)i;

    ts_slice slices[] = {{0, INT64_MAX, 2}, TS_ALL};
    ts_tensor* v;
    ASSERT_OK(ts_view(&v, t, slices));
    ASSERT_TRUE(!ts_is_contiguous(v));

    // Make contiguous copy
    ts_tensor* c;
    ASSERT_OK(ts_contiguous(&c, v));
    ASSERT_TRUE(ts_is_contiguous(c));

    // Values should match the view
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){0, 0}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){0, 3}), 3.0);
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){1, 0}), 8.0);

    ts_free(c); ts_free(v); ts_free(t);
    return 0;
}

static int test_3d_tensor(void)
{
    // 3D tensor simulating a small volume
    ts_tensor* vol;
    int64_t shape[] = {4, 4, 4};
    ASSERT_OK(ts_zeros(&vol, 3, shape, TS_U8));

    ASSERT_TRUE(ts_nelem(vol) == 64);
    ASSERT_TRUE(ts_nbytes(vol) == 64);

    // Set voxel [2,1,3] = 200
    int64_t idx[] = {2, 1, 3};
    ts_set_u8(vol, idx, 200);
    ASSERT_TRUE(ts_get_u8(vol, idx) == 200);

    // Slice out a 2x2x2 sub-volume
    ts_slice slices[] = {{1,3,1}, {1,3,1}, {1,3,1}};
    ts_tensor* sub;
    ASSERT_OK(ts_view(&sub, vol, slices));
    ASSERT_TRUE(sub->shape[0] == 2);
    ASSERT_TRUE(sub->shape[1] == 2);
    ASSERT_TRUE(sub->shape[2] == 2);

    // [2,1,3] in vol = [1,0,2] in sub
    int64_t sub_idx[] = {1, 0, 2};
    ASSERT_TRUE(ts_get_u8(sub, sub_idx) == 200);

    ts_free(sub);
    ts_free(vol);
    return 0;
}

static int test_clone_strided(void)
{
    // Clone a non-contiguous view
    ts_tensor* t;
    int64_t shape[] = {4};
    ASSERT_OK(ts_zeros(&t, 1, shape, TS_I32));
    for (int i = 0; i < 4; i++)
        ((int32_t*)t->data)[i] = i * 10;

    ts_slice s[] = {{0, INT64_MAX, 2}};
    ts_tensor* v;
    ASSERT_OK(ts_view(&v, t, s));

    ts_tensor* c;
    ASSERT_OK(ts_clone(&c, v));
    ASSERT_TRUE(ts_is_contiguous(c));
    ASSERT_TRUE(ts_get_i32(c, (int64_t[]){0}) == 0);
    ASSERT_TRUE(ts_get_i32(c, (int64_t[]){1}) == 20);

    ts_free(c); ts_free(v); ts_free(t);
    return 0;
}

// ── New tests ───────────────────────────────────────────────────────────────

static int test_abs_neg(void)
{
    ts_tensor *a, *b, *c;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    ((float*)a->data)[0] = -3.0f;
    ((float*)a->data)[1] = 0.0f;
    ((float*)a->data)[2] = 5.0f;

    ASSERT_OK(ts_abs(&b, a));
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){0}), 3.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){1}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){2}), 5.0);

    ASSERT_OK(ts_neg(&c, a));
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){0}), 3.0);
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){1}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){2}), -5.0);

    ts_free(a); ts_free(b); ts_free(c);
    return 0;
}

static int test_sqrt_exp_log(void)
{
    ts_tensor *a, *b;
    int64_t shape[] = {2};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F64));
    ((double*)a->data)[0] = 4.0;
    ((double*)a->data)[1] = 9.0;

    ASSERT_OK(ts_sqrt(&b, a));
    ASSERT_EQ_F64(ts_get_f64(b, (int64_t[]){0}), 2.0);
    ASSERT_EQ_F64(ts_get_f64(b, (int64_t[]){1}), 3.0);
    ts_free(b);

    ((double*)a->data)[0] = 0.0;
    ((double*)a->data)[1] = 1.0;
    ASSERT_OK(ts_exp(&b, a));
    ASSERT_EQ_F64(ts_get_f64(b, (int64_t[]){0}), 1.0);
    ASSERT_TRUE(fabs(ts_get_f64(b, (int64_t[]){1}) - exp(1.0)) < 1e-9);
    ts_free(b);

    ((double*)a->data)[0] = 1.0;
    ((double*)a->data)[1] = exp(1.0);
    ASSERT_OK(ts_log(&b, a));
    ASSERT_EQ_F64(ts_get_f64(b, (int64_t[]){0}), 0.0);
    ASSERT_TRUE(fabs(ts_get_f64(b, (int64_t[]){1}) - 1.0) < 1e-9);
    ts_free(b);

    ts_free(a);
    return 0;
}

static int test_clamp(void)
{
    ts_tensor *a, *b;
    int64_t shape[] = {5};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    float vals[] = {-10.0f, 0.0f, 5.0f, 100.0f, 255.0f};
    for (int i = 0; i < 5; i++) ((float*)a->data)[i] = vals[i];

    ASSERT_OK(ts_clamp(&b, a, 0.0, 10.0));
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){0}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){1}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){2}), 5.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){3}), 10.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){4}), 10.0);

    ts_free(a); ts_free(b);
    return 0;
}

static int test_comparison(void)
{
    ts_tensor *a, *b, *eq, *gt, *lt;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    ASSERT_OK(ts_zeros(&b, 1, shape, TS_F32));
    ((float*)a->data)[0] = 1; ((float*)a->data)[1] = 2; ((float*)a->data)[2] = 3;
    ((float*)b->data)[0] = 3; ((float*)b->data)[1] = 2; ((float*)b->data)[2] = 1;

    ASSERT_OK(ts_eq(&eq, a, b));
    ASSERT_OK(ts_gt(&gt, a, b));
    ASSERT_OK(ts_lt(&lt, a, b));

    // eq: [0, 1, 0]
    ASSERT_TRUE(ts_get_u8(eq, (int64_t[]){0}) == 0);
    ASSERT_TRUE(ts_get_u8(eq, (int64_t[]){1}) == 1);
    ASSERT_TRUE(ts_get_u8(eq, (int64_t[]){2}) == 0);

    // gt: [0, 0, 1]
    ASSERT_TRUE(ts_get_u8(gt, (int64_t[]){0}) == 0);
    ASSERT_TRUE(ts_get_u8(gt, (int64_t[]){1}) == 0);
    ASSERT_TRUE(ts_get_u8(gt, (int64_t[]){2}) == 1);

    // lt: [1, 0, 0]
    ASSERT_TRUE(ts_get_u8(lt, (int64_t[]){0}) == 1);
    ASSERT_TRUE(ts_get_u8(lt, (int64_t[]){1}) == 0);
    ASSERT_TRUE(ts_get_u8(lt, (int64_t[]){2}) == 0);

    ts_free(a); ts_free(b); ts_free(eq); ts_free(gt); ts_free(lt);
    return 0;
}

static int test_where(void)
{
    ts_tensor *cond, *a, *b, *result;
    int64_t shape[] = {4};
    ASSERT_OK(ts_zeros(&cond, 1, shape, TS_U8));
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    ASSERT_OK(ts_zeros(&b, 1, shape, TS_F32));

    // cond = [1, 0, 1, 0]
    ((uint8_t*)cond->data)[0] = 1; ((uint8_t*)cond->data)[2] = 1;
    // a = [10, 20, 30, 40]
    for (int i = 0; i < 4; i++) ((float*)a->data)[i] = (float)((i+1)*10);
    // b = [100, 200, 300, 400]
    for (int i = 0; i < 4; i++) ((float*)b->data)[i] = (float)((i+1)*100);

    ASSERT_OK(ts_where(&result, cond, a, b));
    ASSERT_EQ_F64(ts_get_f32(result, (int64_t[]){0}), 10.0);  // cond=1 -> a
    ASSERT_EQ_F64(ts_get_f32(result, (int64_t[]){1}), 200.0); // cond=0 -> b
    ASSERT_EQ_F64(ts_get_f32(result, (int64_t[]){2}), 30.0);  // cond=1 -> a
    ASSERT_EQ_F64(ts_get_f32(result, (int64_t[]){3}), 400.0); // cond=0 -> b

    ts_free(cond); ts_free(a); ts_free(b); ts_free(result);
    return 0;
}

static int test_matmul(void)
{
    // [2,3] x [3,2] -> [2,2]
    ts_tensor *a, *b, *c;
    int64_t sa[] = {2, 3}, sb[] = {3, 2};
    ASSERT_OK(ts_zeros(&a, 2, sa, TS_F32));
    ASSERT_OK(ts_zeros(&b, 2, sb, TS_F32));

    // a = [[1,2,3],[4,5,6]]
    float avals[] = {1,2,3,4,5,6};
    for (int i = 0; i < 6; i++) ((float*)a->data)[i] = avals[i];

    // b = [[7,8],[9,10],[11,12]]
    float bvals[] = {7,8,9,10,11,12};
    for (int i = 0; i < 6; i++) ((float*)b->data)[i] = bvals[i];

    ASSERT_OK(ts_matmul(&c, a, b));
    ASSERT_TRUE(c->shape[0] == 2);
    ASSERT_TRUE(c->shape[1] == 2);

    // [[1*7+2*9+3*11, 1*8+2*10+3*12], [4*7+5*9+6*11, 4*8+5*10+6*12]]
    // = [[58, 64], [139, 154]]
    ASSERT_EQ_F64(ts_get_f64(c, (int64_t[]){0,0}), 58.0);
    ASSERT_EQ_F64(ts_get_f64(c, (int64_t[]){0,1}), 64.0);
    ASSERT_EQ_F64(ts_get_f64(c, (int64_t[]){1,0}), 139.0);
    ASSERT_EQ_F64(ts_get_f64(c, (int64_t[]){1,1}), 154.0);

    ts_free(a); ts_free(b); ts_free(c);
    return 0;
}

static int test_trilinear(void)
{
    // 4x4x4 volume, fill with v[z][y][x] = z*100 + y*10 + x
    ts_tensor* vol;
    int64_t shape[] = {4, 4, 4};
    ASSERT_OK(ts_zeros(&vol, 3, shape, TS_F32));
    for (int z = 0; z < 4; z++)
    for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) {
        int64_t idx[] = {z, y, x};
        ts_set_f32(vol, idx, (float)(z*100 + y*10 + x));
    }

    // Exact voxel — should match nearest
    ASSERT_EQ_F64(ts_sample3d_at(vol, 1.0, 2.0, 3.0, TS_NEAREST), 123.0);
    ASSERT_EQ_F64(ts_sample3d_at(vol, 1.0, 2.0, 3.0, TS_TRILINEAR), 123.0);

    // Midpoint between [0,0,0]=0 and [1,1,1]=111 should average
    double mid = ts_sample3d_at(vol, 0.5, 0.5, 0.5, TS_TRILINEAR);
    // = avg of 8 corners: (0+1+10+11+100+101+110+111)/8 = 55.5
    ASSERT_EQ_F64(mid, 55.5);

    ts_free(vol);
    return 0;
}

static int test_sample3d_batch(void)
{
    // 4x4x4 volume
    ts_tensor* vol;
    int64_t shape[] = {4, 4, 4};
    ASSERT_OK(ts_zeros(&vol, 3, shape, TS_F32));
    for (int z = 0; z < 4; z++)
    for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) {
        int64_t idx[] = {z, y, x};
        ts_set_f32(vol, idx, (float)(z*100 + y*10 + x));
    }

    // Coords: 3 sample points
    ts_tensor* coords;
    int64_t cs[] = {3, 3};
    ASSERT_OK(ts_zeros(&coords, 2, cs, TS_F32));
    // Point 0: (0, 0, 0)
    ts_set_f32(coords, (int64_t[]){0,0}, 0.0f);
    ts_set_f32(coords, (int64_t[]){0,1}, 0.0f);
    ts_set_f32(coords, (int64_t[]){0,2}, 0.0f);
    // Point 1: (1, 2, 3)
    ts_set_f32(coords, (int64_t[]){1,0}, 1.0f);
    ts_set_f32(coords, (int64_t[]){1,1}, 2.0f);
    ts_set_f32(coords, (int64_t[]){1,2}, 3.0f);
    // Point 2: (0.5, 0.5, 0.5)
    ts_set_f32(coords, (int64_t[]){2,0}, 0.5f);
    ts_set_f32(coords, (int64_t[]){2,1}, 0.5f);
    ts_set_f32(coords, (int64_t[]){2,2}, 0.5f);

    ts_tensor* out;
    ASSERT_OK(ts_sample3d(&out, vol, coords, TS_TRILINEAR));
    ASSERT_TRUE(out->shape[0] == 3);

    ASSERT_EQ_F64(ts_get_f32(out, (int64_t[]){0}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(out, (int64_t[]){1}), 123.0);
    ASSERT_TRUE(fabs(ts_get_f32(out, (int64_t[]){2}) - 55.5) < 0.01);

    ts_free(vol); ts_free(coords); ts_free(out);
    return 0;
}

static int test_tricubic(void)
{
    // 8x8x8 volume with linear gradient — tricubic should reproduce linear exactly
    ts_tensor* vol;
    int64_t shape[] = {8, 8, 8};
    ASSERT_OK(ts_zeros(&vol, 3, shape, TS_F64));
    for (int z = 0; z < 8; z++)
    for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
        int64_t idx[] = {z, y, x};
        ts_set_f64(vol, idx, (double)(z + y + x));
    }

    // Sample at (3.5, 4.5, 2.5) — should be 3.5+4.5+2.5 = 10.5
    double v = ts_sample3d_at(vol, 3.5, 4.5, 2.5, TS_TRICUBIC);
    ASSERT_TRUE(fabs(v - 10.5) < 0.01);

    // Exact lattice point
    double v2 = ts_sample3d_at(vol, 3.0, 4.0, 2.0, TS_TRICUBIC);
    ASSERT_EQ_F64(v2, 9.0);

    ts_free(vol);
    return 0;
}

static int test_downsample3d(void)
{
    // 4x4x4 filled with 1.0 -> 2x2x2 filled with 1.0
    ts_tensor *a, *b;
    int64_t shape[] = {4, 4, 4};
    ASSERT_OK(ts_ones(&a, 3, shape, TS_F32));

    ASSERT_OK(ts_downsample3d_2x(&b, a));
    ASSERT_TRUE(b->shape[0] == 2);
    ASSERT_TRUE(b->shape[1] == 2);
    ASSERT_TRUE(b->shape[2] == 2);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){0,0,0}), 1.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){1,1,1}), 1.0);
    ts_free(b);

    // Non-uniform: one octant has 8.0, rest 0.0
    ts_free(a);
    ASSERT_OK(ts_zeros(&a, 3, shape, TS_F32));
    for (int z = 0; z < 2; z++)
    for (int y = 0; y < 2; y++)
    for (int x = 0; x < 2; x++) {
        int64_t idx[] = {z, y, x};
        ts_set_f32(a, idx, 8.0f);
    }
    ASSERT_OK(ts_downsample3d_2x(&b, a));
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){0,0,0}), 8.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){1,1,1}), 0.0);

    ts_free(a); ts_free(b);
    return 0;
}

static int test_paste(void)
{
    ts_tensor *dst, *src;
    int64_t ds[] = {4, 4}, ss[] = {2, 2};
    ASSERT_OK(ts_zeros(&dst, 2, ds, TS_I32));
    ASSERT_OK(ts_ones(&src, 2, ss, TS_I32));

    int64_t offset[] = {1, 1};
    ASSERT_OK(ts_paste(dst, src, offset));

    // [1,1], [1,2], [2,1], [2,2] should be 1
    ASSERT_TRUE(ts_get_i32(dst, (int64_t[]){1,1}) == 1);
    ASSERT_TRUE(ts_get_i32(dst, (int64_t[]){1,2}) == 1);
    ASSERT_TRUE(ts_get_i32(dst, (int64_t[]){2,1}) == 1);
    ASSERT_TRUE(ts_get_i32(dst, (int64_t[]){2,2}) == 1);
    // Outside should still be 0
    ASSERT_TRUE(ts_get_i32(dst, (int64_t[]){0,0}) == 0);
    ASSERT_TRUE(ts_get_i32(dst, (int64_t[]){3,3}) == 0);

    ts_free(dst); ts_free(src);
    return 0;
}

static double scale_by_2(double v, void* ud)
{
    (void)ud;
    return v * 2.0;
}

static int test_map(void)
{
    ts_tensor *a, *b;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    ((float*)a->data)[0] = 1; ((float*)a->data)[1] = 2; ((float*)a->data)[2] = 3;

    ASSERT_OK(ts_map(&b, a, scale_by_2, NULL));
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){0}), 2.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){1}), 4.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){2}), 6.0);

    ts_free(b);

    // In-place
    ASSERT_OK(ts_map_inplace(a, scale_by_2, NULL));
    ASSERT_EQ_F64(ts_get_f32(a, (int64_t[]){0}), 2.0);
    ASSERT_EQ_F64(ts_get_f32(a, (int64_t[]){1}), 4.0);

    ts_free(a);
    return 0;
}

static int test_print(void)
{
    ts_tensor* t;
    int64_t shape[] = {2, 3};
    ASSERT_OK(ts_zeros(&t, 2, shape, TS_F32));

    char buf[64];
    int n = ts_shape_str(t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strcmp(buf, "[2, 3]") == 0);

    // Just verify ts_print doesn't crash
    ts_print(t, stdout);
    ts_free(t);
    return 0;
}

static int test_inplace_ops(void)
{
    ts_tensor *a, *b;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    ASSERT_OK(ts_zeros(&b, 1, shape, TS_F32));
    ((float*)a->data)[0] = 1; ((float*)a->data)[1] = 2; ((float*)a->data)[2] = 3;
    ((float*)b->data)[0] = 10; ((float*)b->data)[1] = 20; ((float*)b->data)[2] = 30;

    ASSERT_OK(ts_add_inplace(a, b));
    ASSERT_EQ_F64(ts_get_f32(a, (int64_t[]){0}), 11.0);
    ASSERT_EQ_F64(ts_get_f32(a, (int64_t[]){1}), 22.0);
    ASSERT_EQ_F64(ts_get_f32(a, (int64_t[]){2}), 33.0);

    ASSERT_OK(ts_scale_inplace(a, 0.5));
    ASSERT_EQ_F64(ts_get_f32(a, (int64_t[]){0}), 5.5);

    ts_free(a); ts_free(b);
    return 0;
}

static int test_inplace_broadcast(void)
{
    // a: [3, 3], b: [1, 3] — broadcast b across rows
    ts_tensor *a, *b;
    int64_t sa[] = {3, 3}, sb[] = {1, 3};
    ASSERT_OK(ts_ones(&a, 2, sa, TS_F32));
    ASSERT_OK(ts_zeros(&b, 2, sb, TS_F32));
    ((float*)b->data)[0] = 10; ((float*)b->data)[1] = 20; ((float*)b->data)[2] = 30;

    ASSERT_OK(ts_mul_inplace(a, b));
    ASSERT_EQ_F64(ts_get_f32(a, (int64_t[]){0, 0}), 10.0);
    ASSERT_EQ_F64(ts_get_f32(a, (int64_t[]){2, 2}), 30.0);

    ts_free(a); ts_free(b);
    return 0;
}

static int test_dot(void)
{
    ts_tensor *a, *b, *d;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    ASSERT_OK(ts_zeros(&b, 1, shape, TS_F32));
    ((float*)a->data)[0] = 1; ((float*)a->data)[1] = 2; ((float*)a->data)[2] = 3;
    ((float*)b->data)[0] = 4; ((float*)b->data)[1] = 5; ((float*)b->data)[2] = 6;

    ASSERT_OK(ts_dot(&d, a, b));
    ASSERT_EQ_F64(ts_get_f64(d, (int64_t[]){0}), 32.0); // 4+10+18

    ts_free(a); ts_free(b); ts_free(d);
    return 0;
}

static int test_norm(void)
{
    ts_tensor *a, *n;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F64));
    ((double*)a->data)[0] = 3; ((double*)a->data)[1] = 4; ((double*)a->data)[2] = 0;

    ASSERT_OK(ts_norm(&n, a));
    ASSERT_EQ_F64(ts_get_f64(n, (int64_t[]){0}), 5.0);

    ts_free(a); ts_free(n);
    return 0;
}

static int test_argmin_argmax(void)
{
    ts_tensor* t;
    int64_t shape[] = {5};
    ASSERT_OK(ts_zeros(&t, 1, shape, TS_F32));
    float vals[] = {3, 1, 4, 0.5f, 5};
    for (int i = 0; i < 5; i++) ((float*)t->data)[i] = vals[i];

    ASSERT_TRUE(ts_argmin(t) == 3);
    ASSERT_TRUE(ts_argmax(t) == 4);

    ts_free(t);
    return 0;
}

static int test_cat(void)
{
    ts_tensor *a, *b, *c;
    int64_t sa[] = {2, 3}, sb[] = {1, 3};
    ASSERT_OK(ts_zeros(&a, 2, sa, TS_F32));
    ASSERT_OK(ts_ones(&b, 2, sb, TS_F32));

    const ts_tensor* tensors[] = {a, b};
    ASSERT_OK(ts_cat(&c, tensors, 2, 0));
    ASSERT_TRUE(c->shape[0] == 3);
    ASSERT_TRUE(c->shape[1] == 3);
    // First 2 rows are 0, last row is 1
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){0, 0}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){2, 0}), 1.0);
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){2, 2}), 1.0);

    ts_free(a); ts_free(b); ts_free(c);
    return 0;
}

static int test_cat_axis1(void)
{
    ts_tensor *a, *b, *c;
    int64_t sa[] = {2, 2}, sb[] = {2, 3};
    ASSERT_OK(ts_zeros(&a, 2, sa, TS_I32));
    ASSERT_OK(ts_ones(&b, 2, sb, TS_I32));

    const ts_tensor* tensors[] = {a, b};
    ASSERT_OK(ts_cat(&c, tensors, 2, 1));
    ASSERT_TRUE(c->shape[0] == 2);
    ASSERT_TRUE(c->shape[1] == 5);
    ASSERT_TRUE(ts_get_i32(c, (int64_t[]){0, 0}) == 0);
    ASSERT_TRUE(ts_get_i32(c, (int64_t[]){0, 2}) == 1);
    ASSERT_TRUE(ts_get_i32(c, (int64_t[]){1, 4}) == 1);

    ts_free(a); ts_free(b); ts_free(c);
    return 0;
}

static int test_stack(void)
{
    ts_tensor *a, *b, *c;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    ASSERT_OK(ts_ones(&b, 1, shape, TS_F32));
    ((float*)a->data)[0] = 1; ((float*)a->data)[1] = 2; ((float*)a->data)[2] = 3;

    const ts_tensor* tensors[] = {a, b};
    ASSERT_OK(ts_stack(&c, tensors, 2, 0));
    ASSERT_TRUE(c->ndim == 2);
    ASSERT_TRUE(c->shape[0] == 2);
    ASSERT_TRUE(c->shape[1] == 3);
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){0, 0}), 1.0);
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){0, 2}), 3.0);
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){1, 0}), 1.0);
    ASSERT_EQ_F64(ts_get_f32(c, (int64_t[]){1, 1}), 1.0);

    ts_free(a); ts_free(b); ts_free(c);
    return 0;
}

static int test_flip(void)
{
    ts_tensor *a, *f;
    int64_t shape[] = {4};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_I32));
    for (int i = 0; i < 4; i++) ((int32_t*)a->data)[i] = i;

    ASSERT_OK(ts_flip(&f, a, 0));
    ASSERT_TRUE(ts_get_i32(f, (int64_t[]){0}) == 3);
    ASSERT_TRUE(ts_get_i32(f, (int64_t[]){1}) == 2);
    ASSERT_TRUE(ts_get_i32(f, (int64_t[]){2}) == 1);
    ASSERT_TRUE(ts_get_i32(f, (int64_t[]){3}) == 0);

    // Flip is a view — modifying it modifies original
    ts_set_i32(f, (int64_t[]){0}, 99);
    ASSERT_TRUE(ts_get_i32(a, (int64_t[]){3}) == 99);

    ts_free(f); ts_free(a);
    return 0;
}

static int test_flip_2d(void)
{
    ts_tensor *a, *f;
    int64_t shape[] = {2, 3};
    ASSERT_OK(ts_zeros(&a, 2, shape, TS_F32));
    // [[0,1,2],[3,4,5]]
    for (int i = 0; i < 6; i++) ((float*)a->data)[i] = (float)i;

    // Flip axis 1 -> [[2,1,0],[5,4,3]]
    ASSERT_OK(ts_flip(&f, a, 1));
    ASSERT_EQ_F64(ts_get_f32(f, (int64_t[]){0, 0}), 2.0);
    ASSERT_EQ_F64(ts_get_f32(f, (int64_t[]){0, 2}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(f, (int64_t[]){1, 0}), 5.0);

    ts_free(f); ts_free(a);
    return 0;
}

static int test_repeat(void)
{
    ts_tensor *a, *r;
    int64_t shape[] = {2};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_I32));
    ((int32_t*)a->data)[0] = 10;
    ((int32_t*)a->data)[1] = 20;

    ASSERT_OK(ts_repeat(&r, a, 0, 3));
    ASSERT_TRUE(r->shape[0] == 6);
    ASSERT_TRUE(ts_get_i32(r, (int64_t[]){0}) == 10);
    ASSERT_TRUE(ts_get_i32(r, (int64_t[]){1}) == 20);
    ASSERT_TRUE(ts_get_i32(r, (int64_t[]){2}) == 10);
    ASSERT_TRUE(ts_get_i32(r, (int64_t[]){5}) == 20);

    ts_free(a); ts_free(r);
    return 0;
}

static int test_copy(void)
{
    ts_tensor *a, *b;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    ASSERT_OK(ts_zeros(&b, 1, shape, TS_F32));
    ((float*)a->data)[0] = 1; ((float*)a->data)[1] = 2; ((float*)a->data)[2] = 3;

    ASSERT_OK(ts_copy(b, a));
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){0}), 1.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){2}), 3.0);

    // Independent
    ts_set_f32(a, (int64_t[]){0}, 99.0f);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){0}), 1.0);

    ts_free(a); ts_free(b);
    return 0;
}

static int test_flatten(void)
{
    ts_tensor *a, *f;
    int64_t shape[] = {2, 3};
    ASSERT_OK(ts_zeros(&a, 2, shape, TS_F32));
    for (int i = 0; i < 6; i++) ((float*)a->data)[i] = (float)i;

    ASSERT_OK(ts_flatten(&f, a));
    ASSERT_TRUE(f->ndim == 1);
    ASSERT_TRUE(f->shape[0] == 6);
    ASSERT_EQ_F64(ts_get_f32(f, (int64_t[]){3}), 3.0);

    ts_free(f); ts_free(a);
    return 0;
}

static int test_pool_basic(void)
{
    ts_pool* pool;
    ASSERT_OK(ts_pool_create(&pool, 4));
    ASSERT_TRUE(ts_pool_nthreads(pool) == 4);

    // Submit nothing, wait should return immediately
    ts_pool_wait(pool);

    ts_pool_destroy(pool);
    return 0;
}

static void increment_task(void* arg)
{
    int* val = (int*)arg;
    __atomic_fetch_add(val, 1, __ATOMIC_SEQ_CST);
}

static int test_pool_tasks(void)
{
    ts_pool* pool;
    ASSERT_OK(ts_pool_create(&pool, 4));

    int counter = 0;
    for (int i = 0; i < 1000; i++)
        ts_pool_submit(pool, increment_task, &counter);

    ts_pool_wait(pool);
    ASSERT_TRUE(counter == 1000);

    ts_pool_destroy(pool);
    return 0;
}

static double double_val(double v, void* ud)
{
    (void)ud;
    return v * 2.0;
}

static int test_parallel_map(void)
{
    ts_pool* pool;
    ASSERT_OK(ts_pool_create(&pool, 4));

    ts_tensor *a, *b;
    int64_t shape[] = {10000};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    for (int64_t i = 0; i < 10000; i++)
        ((float*)a->data)[i] = (float)i;

    ASSERT_OK(ts_parallel_map(&b, a, double_val, NULL, pool));

    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){0}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){1}), 2.0);
    ASSERT_EQ_F64(ts_get_f32(b, (int64_t[]){9999}), 19998.0);

    ts_free(a); ts_free(b);
    ts_pool_destroy(pool);
    return 0;
}

typedef struct {
    float* data;
} pfor_ctx;

static void sum_range(int64_t start, int64_t end, void* userdata)
{
    pfor_ctx* ctx = (pfor_ctx*)userdata;
    for (int64_t i = start; i < end; i++)
        ctx->data[i] *= 3.0f;
}

static int test_parallel_for(void)
{
    ts_pool* pool;
    ASSERT_OK(ts_pool_create(&pool, 4));

    ts_tensor* a;
    int64_t shape[] = {1000};
    ASSERT_OK(ts_ones(&a, 1, shape, TS_F32));

    pfor_ctx ctx = { .data = (float*)a->data };
    ts_parallel_for(pool, 1000, sum_range, &ctx);

    // Every element should now be 3.0
    ASSERT_EQ_F64(ts_get_f32(a, (int64_t[]){0}), 3.0);
    ASSERT_EQ_F64(ts_get_f32(a, (int64_t[]){999}), 3.0);

    ts_free(a);
    ts_pool_destroy(pool);
    return 0;
}

static int test_arange(void)
{
    ts_tensor* t;
    ASSERT_OK(ts_arange(&t, 5));
    ASSERT_TRUE(t->ndim == 1);
    ASSERT_TRUE(t->shape[0] == 5);
    ASSERT_TRUE(ts_get_i64(t, (int64_t[]){0}) == 0);
    ASSERT_TRUE(ts_get_i64(t, (int64_t[]){4}) == 4);
    ts_free(t);
    return 0;
}

static int test_linspace(void)
{
    ts_tensor* t;
    ASSERT_OK(ts_linspace(&t, 0.0, 1.0, 5));
    ASSERT_EQ_F64(ts_get_f64(t, (int64_t[]){0}), 0.0);
    ASSERT_EQ_F64(ts_get_f64(t, (int64_t[]){2}), 0.5);
    ASSERT_EQ_F64(ts_get_f64(t, (int64_t[]){4}), 1.0);
    ts_free(t);
    return 0;
}

static int test_eye(void)
{
    ts_tensor* t;
    ASSERT_OK(ts_eye(&t, 3));
    ASSERT_TRUE(t->shape[0] == 3 && t->shape[1] == 3);
    ASSERT_EQ_F64(ts_get_f64(t, (int64_t[]){0,0}), 1.0);
    ASSERT_EQ_F64(ts_get_f64(t, (int64_t[]){0,1}), 0.0);
    ASSERT_EQ_F64(ts_get_f64(t, (int64_t[]){1,1}), 1.0);
    ASSERT_EQ_F64(ts_get_f64(t, (int64_t[]){2,2}), 1.0);
    ts_free(t);
    return 0;
}

static int test_pad_zero(void)
{
    ts_tensor *a, *p;
    int64_t shape[] = {3, 3};
    ASSERT_OK(ts_ones(&a, 2, shape, TS_F32));

    int64_t pad[][2] = {{1, 1}, {2, 2}};
    ASSERT_OK(ts_pad(&p, a, pad, TS_PAD_ZERO));
    ASSERT_TRUE(p->shape[0] == 5);
    ASSERT_TRUE(p->shape[1] == 7);

    // Corners should be zero
    ASSERT_EQ_F64(ts_get_f32(p, (int64_t[]){0, 0}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(p, (int64_t[]){4, 6}), 0.0);
    // Interior should be 1
    ASSERT_EQ_F64(ts_get_f32(p, (int64_t[]){1, 2}), 1.0);
    ASSERT_EQ_F64(ts_get_f32(p, (int64_t[]){3, 4}), 1.0);

    ts_free(a); ts_free(p);
    return 0;
}

static int test_pad_edge(void)
{
    ts_tensor *a, *p;
    int64_t shape[] = {3};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    ((float*)a->data)[0] = 10; ((float*)a->data)[1] = 20; ((float*)a->data)[2] = 30;

    int64_t pad[][2] = {{2, 2}};
    ASSERT_OK(ts_pad(&p, a, pad, TS_PAD_EDGE));
    ASSERT_TRUE(p->shape[0] == 7);
    // Edge replicated: [10, 10, 10, 20, 30, 30, 30]
    ASSERT_EQ_F64(ts_get_f32(p, (int64_t[]){0}), 10.0);
    ASSERT_EQ_F64(ts_get_f32(p, (int64_t[]){1}), 10.0);
    ASSERT_EQ_F64(ts_get_f32(p, (int64_t[]){3}), 20.0);
    ASSERT_EQ_F64(ts_get_f32(p, (int64_t[]){5}), 30.0);
    ASSERT_EQ_F64(ts_get_f32(p, (int64_t[]){6}), 30.0);

    ts_free(a); ts_free(p);
    return 0;
}

static int test_conv3d(void)
{
    // 5x5x5 volume, convolve with averaging 3x3x3 kernel
    ts_tensor *vol, *kern, *out;
    int64_t vs[] = {5, 5, 5};
    ASSERT_OK(ts_ones(&vol, 3, vs, TS_F32));

    int64_t ks[] = {3, 3, 3};
    ts_tensor* k;
    ASSERT_OK(ts_ones(&k, 3, ks, TS_F64));
    ASSERT_OK(ts_scale(&kern, k, 1.0/27.0));
    ts_free(k);

    ASSERT_OK(ts_conv3d(&out, vol, kern));
    // Center of a constant-1 volume convolved with averaging kernel = 1.0
    ASSERT_TRUE(fabs(ts_get_f64(out, (int64_t[]){2,2,2}) - 1.0) < 1e-9);
    // Corner [0,0,0] only covers 2x2x2 = 8 voxels out of 27
    ASSERT_TRUE(fabs(ts_get_f64(out, (int64_t[]){0,0,0}) - 8.0/27.0) < 1e-9);

    ts_free(vol); ts_free(kern); ts_free(out);
    return 0;
}

static int test_std(void)
{
    ts_tensor *a, *s;
    int64_t shape[] = {4};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F64));
    ((double*)a->data)[0] = 2; ((double*)a->data)[1] = 4;
    ((double*)a->data)[2] = 4; ((double*)a->data)[3] = 4;
    // mean=3.5, var=((2-3.5)^2+(4-3.5)^2*3)/4 = (2.25+0.75)/4 = 0.75
    // std = sqrt(0.75)

    ASSERT_OK(ts_std(&s, a));
    ASSERT_TRUE(fabs(ts_get_f64(s, (int64_t[]){0}) - sqrt(0.75)) < 1e-9);

    ts_free(a); ts_free(s);
    return 0;
}

static int test_histogram(void)
{
    ts_tensor *a, *h;
    int64_t shape[] = {10};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    // Values: 0,1,2,3,4,5,6,7,8,9
    for (int i = 0; i < 10; i++) ((float*)a->data)[i] = (float)i;

    // 5 bins from 0 to 10
    ASSERT_OK(ts_histogram(&h, a, 5, 0.0, 10.0));
    ASSERT_TRUE(h->shape[0] == 5);
    // Each bin should have 2 values: [0,1], [2,3], [4,5], [6,7], [8,9]
    ASSERT_TRUE(ts_get_i64(h, (int64_t[]){0}) == 2);
    ASSERT_TRUE(ts_get_i64(h, (int64_t[]){4}) == 2);

    ts_free(a); ts_free(h);
    return 0;
}

static int test_threshold(void)
{
    ts_tensor *a, *t;
    int64_t shape[] = {5};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_F32));
    float vals[] = {0.1f, 0.5f, 0.9f, 0.3f, 0.7f};
    for (int i = 0; i < 5; i++) ((float*)a->data)[i] = vals[i];

    ASSERT_OK(ts_threshold(&t, a, 0.5));
    ASSERT_TRUE(ts_get_u8(t, (int64_t[]){0}) == 0);
    ASSERT_TRUE(ts_get_u8(t, (int64_t[]){1}) == 0);  // 0.5 not > 0.5
    ASSERT_TRUE(ts_get_u8(t, (int64_t[]){2}) == 1);
    ASSERT_TRUE(ts_get_u8(t, (int64_t[]){3}) == 0);
    ASSERT_TRUE(ts_get_u8(t, (int64_t[]){4}) == 1);

    ts_free(a); ts_free(t);
    return 0;
}

static int test_normalize(void)
{
    ts_tensor *a, *n;
    int64_t shape[] = {4};
    ASSERT_OK(ts_zeros(&a, 1, shape, TS_U8));
    ((uint8_t*)a->data)[0] = 0;
    ((uint8_t*)a->data)[1] = 128;
    ((uint8_t*)a->data)[2] = 255;
    ((uint8_t*)a->data)[3] = 64;

    ASSERT_OK(ts_normalize(&n, a));
    ASSERT_TRUE(n->dtype == TS_F32);
    ASSERT_EQ_F64(ts_get_f32(n, (int64_t[]){0}), 0.0);
    ASSERT_EQ_F64(ts_get_f32(n, (int64_t[]){2}), 1.0);
    ASSERT_TRUE(ts_get_f32(n, (int64_t[]){1}) > 0.49 && ts_get_f32(n, (int64_t[]){1}) < 0.51);

    ts_free(a); ts_free(n);
    return 0;
}

// ── Virtual tensor tests ────────────────────────────────────────────────────

// Simulated backend: v[z][y][x] = z*10000 + y*100 + x
// In real use, this would call into mlcache → zarr → vl264 → s3/disk.
static double test_voxel_fn(const int64_t* coords, void* userdata)
{
    (void)userdata;
    return (double)(coords[0] * 10000 + coords[1] * 100 + coords[2]);
}

static ts_status test_region_fn(const int64_t* start, const int64_t* stop,
                                void* buf, void* userdata)
{
    (void)userdata;
    double* out = (double*)buf;
    int64_t i = 0;
    for (int64_t z = start[0]; z < stop[0]; z++)
    for (int64_t y = start[1]; y < stop[1]; y++)
    for (int64_t x = start[2]; x < stop[2]; x++)
        out[i++] = (double)(z * 10000 + y * 100 + x);
    return TS_OK;
}

static int test_virtual_basic(void)
{
    ts_virtual* vt;
    int64_t shape[] = {16, 16, 16};
    ASSERT_OK(ts_virtual_create(&vt, 3, shape, TS_F64,
                                test_voxel_fn, test_region_fn, NULL));

    ASSERT_TRUE(ts_virtual_ndim(vt) == 3);
    ASSERT_TRUE(ts_virtual_shape(vt)[0] == 16);

    double v = ts_virtual_get3(vt, 0, 0, 0);
    ASSERT_EQ_F64(v, 0.0);

    v = ts_virtual_get3(vt, 1, 2, 3);
    ASSERT_EQ_F64(v, 10203.0);

    v = ts_virtual_get3(vt, 5, 6, 7);
    ASSERT_EQ_F64(v, 50607.0);

    ts_virtual_free(vt);
    return 0;
}

static int test_virtual_read_region(void)
{
    ts_virtual* vt;
    int64_t shape[] = {16, 16, 16};
    ASSERT_OK(ts_virtual_create(&vt, 3, shape, TS_F64,
                                test_voxel_fn, test_region_fn, NULL));

    ts_tensor* region;
    int64_t start[] = {3, 3, 3}, stop[] = {5, 5, 5};
    ASSERT_OK(ts_virtual_read(vt, &region, start, stop));

    ASSERT_TRUE(region->shape[0] == 2);
    ASSERT_TRUE(region->shape[1] == 2);
    ASSERT_TRUE(region->shape[2] == 2);

    ASSERT_EQ_F64(ts_get_f64(region, (int64_t[]){0,0,0}), 30303.0);
    ASSERT_EQ_F64(ts_get_f64(region, (int64_t[]){1,1,1}), 40404.0);

    ts_free(region);
    ts_virtual_free(vt);
    return 0;
}

static int test_virtual_interpolation(void)
{
    ts_virtual* vt;
    int64_t shape[] = {16, 16, 16};
    ASSERT_OK(ts_virtual_create(&vt, 3, shape, TS_F64,
                                test_voxel_fn, NULL, NULL));

    // Exact voxel
    double v = ts_virtual_sample(vt, 2.0, 3.0, 1.0, TS_TRILINEAR);
    ASSERT_EQ_F64(v, 20301.0);

    // Nearest
    v = ts_virtual_sample(vt, 2.4, 3.6, 1.2, TS_NEAREST);
    ASSERT_EQ_F64(v, 20401.0);

    ts_virtual_free(vt);
    return 0;
}

static int test_virtual_fallback(void)
{
    // No region_fn — should fall back to per-voxel reads
    ts_virtual* vt;
    int64_t shape[] = {8, 8, 8};
    ASSERT_OK(ts_virtual_create(&vt, 3, shape, TS_F64,
                                test_voxel_fn, NULL, NULL));

    ts_tensor* region;
    int64_t start[] = {0, 0, 0}, stop[] = {2, 2, 2};
    ASSERT_OK(ts_virtual_read(vt, &region, start, stop));

    ASSERT_EQ_F64(ts_get_f64(region, (int64_t[]){0,0,0}), 0.0);
    ASSERT_EQ_F64(ts_get_f64(region, (int64_t[]){1,1,1}), 10101.0);

    ts_free(region);
    ts_virtual_free(vt);
    return 0;
}

static int test_print_stats_fn(void)
{
    ts_tensor* t;
    int64_t shape[] = {4};
    ASSERT_OK(ts_zeros(&t, 1, shape, TS_F32));
    ((float*)t->data)[0] = 1; ((float*)t->data)[1] = 2;
    ((float*)t->data)[2] = 3; ((float*)t->data)[3] = 4;

    // Just verify it doesn't crash
    ts_print_stats(t, stdout);
    ts_free(t);
    return 0;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(void)
{
    int failures = 0;
    int total = 0;
    printf("libtensor %s — tests\n", ts_version_str());

    RUN_TEST(test_version);
    RUN_TEST(test_dtype);
    RUN_TEST(test_zeros);
    RUN_TEST(test_ones);
    RUN_TEST(test_full);
    RUN_TEST(test_from_data);
    RUN_TEST(test_clone);
    RUN_TEST(test_set_get);
    RUN_TEST(test_view_slice);
    RUN_TEST(test_view_step);
    RUN_TEST(test_reshape);
    RUN_TEST(test_transpose);
    RUN_TEST(test_squeeze_unsqueeze);
    RUN_TEST(test_add);
    RUN_TEST(test_broadcast);
    RUN_TEST(test_scale);
    RUN_TEST(test_cast);
    RUN_TEST(test_sum);
    RUN_TEST(test_mean);
    RUN_TEST(test_min_max);
    RUN_TEST(test_contiguous);
    RUN_TEST(test_3d_tensor);
    RUN_TEST(test_clone_strided);
    RUN_TEST(test_abs_neg);
    RUN_TEST(test_sqrt_exp_log);
    RUN_TEST(test_clamp);
    RUN_TEST(test_comparison);
    RUN_TEST(test_where);
    RUN_TEST(test_matmul);
    RUN_TEST(test_trilinear);
    RUN_TEST(test_sample3d_batch);
    RUN_TEST(test_tricubic);
    RUN_TEST(test_downsample3d);
    RUN_TEST(test_paste);
    RUN_TEST(test_map);
    RUN_TEST(test_print);
    RUN_TEST(test_inplace_ops);
    RUN_TEST(test_inplace_broadcast);
    RUN_TEST(test_dot);
    RUN_TEST(test_norm);
    RUN_TEST(test_argmin_argmax);
    RUN_TEST(test_cat);
    RUN_TEST(test_cat_axis1);
    RUN_TEST(test_stack);
    RUN_TEST(test_flip);
    RUN_TEST(test_flip_2d);
    RUN_TEST(test_repeat);
    RUN_TEST(test_copy);
    RUN_TEST(test_flatten);
    RUN_TEST(test_pool_basic);
    RUN_TEST(test_pool_tasks);
    RUN_TEST(test_parallel_map);
    RUN_TEST(test_parallel_for);
    RUN_TEST(test_arange);
    RUN_TEST(test_linspace);
    RUN_TEST(test_eye);
    RUN_TEST(test_pad_zero);
    RUN_TEST(test_pad_edge);
    RUN_TEST(test_conv3d);
    RUN_TEST(test_std);
    RUN_TEST(test_histogram);
    RUN_TEST(test_threshold);
    RUN_TEST(test_normalize);
    RUN_TEST(test_virtual_basic);
    RUN_TEST(test_virtual_read_region);
    RUN_TEST(test_virtual_interpolation);
    RUN_TEST(test_virtual_fallback);
    RUN_TEST(test_print_stats_fn);

    total = 70;
    printf("\n%d/%d tests passed\n", total - failures, total);
    return failures ? 1 : 0;
}
