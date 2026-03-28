// geometry.h -- Pure C23 geometry library for 3D vision and transforms
// Single-header: declare API, then #define GM_IMPLEMENTATION in one .c file.
// Replaces OpenCV geometry ops and Eigen small-matrix ops in VC3D.
#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// -- Version -----------------------------------------------------------------

#define GM_VERSION_MAJOR 0
#define GM_VERSION_MINOR 1
#define GM_VERSION_PATCH 0

// -- C23 Compat --------------------------------------------------------------

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define GM_NODISCARD    [[nodiscard]]
  #define GM_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define GM_NODISCARD    __attribute__((warn_unused_result))
  #define GM_MAYBE_UNUSED __attribute__((unused))
#else
  #define GM_NODISCARD
  #define GM_MAYBE_UNUSED
#endif

// -- Linkage -----------------------------------------------------------------

#ifndef GMDEF
  #ifdef GM_STATIC
    #define GMDEF static
  #else
    #define GMDEF extern
  #endif
#endif

#ifndef GM_INLINE
  #define GM_INLINE static inline
#endif

// -- Allocator Hooks ---------------------------------------------------------

#ifndef GM_MALLOC
  #include <stdlib.h>
  #define GM_MALLOC(sz)       malloc(sz)
  #define GM_REALLOC(p, sz)   realloc(p, sz)
  #define GM_FREE(p)          free(p)
  #define GM_CALLOC(n, sz)    calloc(n, sz)
#endif

// -- Constants ---------------------------------------------------------------

#ifndef GM_PI
  #define GM_PI 3.14159265358979323846
#endif

#define GM_EPS 1e-12

// -- Status Codes ------------------------------------------------------------

typedef enum gm_status {
    GM_OK = 0,
    GM_ERR_NULL_ARG,
    GM_ERR_SINGULAR,      // matrix not invertible
    GM_ERR_CONVERGENCE,   // iterative method did not converge
    GM_ERR_INVALID,       // invalid parameter
    GM_ERR_ALLOC,         // allocation failure
} gm_status;

// -- Vector Types (value types) ----------------------------------------------

typedef struct { float  x, y; }       gm_vec2f;
typedef struct { double x, y; }       gm_vec2d;
typedef struct { float  x, y, z; }    gm_vec3f;
typedef struct { double x, y, z; }    gm_vec3d;
typedef struct { float  x, y, z, w; } gm_vec4f;
typedef struct { int    x, y, z; }    gm_vec3i;

// -- Matrix Types ------------------------------------------------------------

typedef struct { double m[9]; }  gm_mat3;  // 3x3 row-major
typedef struct { double m[16]; } gm_mat4;  // 4x4 row-major

// -- Bounding Box Types ------------------------------------------------------

typedef struct { gm_vec3f lo, hi; } gm_bbox3f;
typedef struct { gm_vec3d lo, hi; } gm_bbox3d;

// -- Quaternion Type ---------------------------------------------------------

typedef struct { double w, x, y, z; } gm_quat;

// -- Utilities ---------------------------------------------------------------

GM_INLINE double gm_deg2rad(double deg) { return deg * (GM_PI / 180.0); }
GM_INLINE double gm_rad2deg(double rad) { return rad * (180.0 / GM_PI); }
GM_INLINE float  gm_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
GM_INLINE double gm_clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

GMDEF const char* gm_status_str(gm_status s);
GMDEF const char* gm_version_str(void);

// -- Vec2f Ops ---------------------------------------------------------------

GM_INLINE gm_vec2f gm_add2f(gm_vec2f a, gm_vec2f b) { return (gm_vec2f){a.x+b.x, a.y+b.y}; }
GM_INLINE gm_vec2f gm_sub2f(gm_vec2f a, gm_vec2f b) { return (gm_vec2f){a.x-b.x, a.y-b.y}; }
GM_INLINE gm_vec2f gm_mul2f(gm_vec2f a, gm_vec2f b) { return (gm_vec2f){a.x*b.x, a.y*b.y}; }
GM_INLINE gm_vec2f gm_scale2f(gm_vec2f a, float s) { return (gm_vec2f){a.x*s, a.y*s}; }
GM_INLINE float    gm_dot2f(gm_vec2f a, gm_vec2f b) { return a.x*b.x + a.y*b.y; }
GM_INLINE float    gm_len2f(gm_vec2f a) { return sqrtf(gm_dot2f(a, a)); }
GM_INLINE gm_vec2f gm_normalize2f(gm_vec2f a) { float l = gm_len2f(a); return l > 0 ? gm_scale2f(a, 1.0f/l) : a; }
GM_INLINE float    gm_dist2f(gm_vec2f a, gm_vec2f b) { return gm_len2f(gm_sub2f(a, b)); }
GM_INLINE gm_vec2f gm_lerp2f(gm_vec2f a, gm_vec2f b, float t) { return gm_add2f(gm_scale2f(a, 1.0f-t), gm_scale2f(b, t)); }

// -- Vec2d Ops ---------------------------------------------------------------

GM_INLINE gm_vec2d gm_add2d(gm_vec2d a, gm_vec2d b) { return (gm_vec2d){a.x+b.x, a.y+b.y}; }
GM_INLINE gm_vec2d gm_sub2d(gm_vec2d a, gm_vec2d b) { return (gm_vec2d){a.x-b.x, a.y-b.y}; }
GM_INLINE gm_vec2d gm_mul2d(gm_vec2d a, gm_vec2d b) { return (gm_vec2d){a.x*b.x, a.y*b.y}; }
GM_INLINE gm_vec2d gm_scale2d(gm_vec2d a, double s) { return (gm_vec2d){a.x*s, a.y*s}; }
GM_INLINE double   gm_dot2d(gm_vec2d a, gm_vec2d b) { return a.x*b.x + a.y*b.y; }
GM_INLINE double   gm_len2d(gm_vec2d a) { return sqrt(gm_dot2d(a, a)); }
GM_INLINE gm_vec2d gm_normalize2d(gm_vec2d a) { double l = gm_len2d(a); return l > 0 ? gm_scale2d(a, 1.0/l) : a; }
GM_INLINE double   gm_dist2d(gm_vec2d a, gm_vec2d b) { return gm_len2d(gm_sub2d(a, b)); }
GM_INLINE gm_vec2d gm_lerp2d(gm_vec2d a, gm_vec2d b, double t) { return gm_add2d(gm_scale2d(a, 1.0-t), gm_scale2d(b, t)); }

// -- Vec3f Ops ---------------------------------------------------------------

GM_INLINE gm_vec3f gm_add3f(gm_vec3f a, gm_vec3f b) { return (gm_vec3f){a.x+b.x, a.y+b.y, a.z+b.z}; }
GM_INLINE gm_vec3f gm_sub3f(gm_vec3f a, gm_vec3f b) { return (gm_vec3f){a.x-b.x, a.y-b.y, a.z-b.z}; }
GM_INLINE gm_vec3f gm_mul3f(gm_vec3f a, gm_vec3f b) { return (gm_vec3f){a.x*b.x, a.y*b.y, a.z*b.z}; }
GM_INLINE gm_vec3f gm_scale3f(gm_vec3f a, float s) { return (gm_vec3f){a.x*s, a.y*s, a.z*s}; }
GM_INLINE float    gm_dot3f(gm_vec3f a, gm_vec3f b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
GM_INLINE gm_vec3f gm_cross3f(gm_vec3f a, gm_vec3f b) {
    return (gm_vec3f){a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
GM_INLINE float    gm_len3f(gm_vec3f a) { return sqrtf(gm_dot3f(a, a)); }
GM_INLINE gm_vec3f gm_normalize3f(gm_vec3f a) { float l = gm_len3f(a); return l > 0 ? gm_scale3f(a, 1.0f/l) : a; }
GM_INLINE float    gm_dist3f(gm_vec3f a, gm_vec3f b) { return gm_len3f(gm_sub3f(a, b)); }
GM_INLINE gm_vec3f gm_lerp3f(gm_vec3f a, gm_vec3f b, float t) { return gm_add3f(gm_scale3f(a, 1.0f-t), gm_scale3f(b, t)); }

// -- Vec3d Ops ---------------------------------------------------------------

GM_INLINE gm_vec3d gm_add3d(gm_vec3d a, gm_vec3d b) { return (gm_vec3d){a.x+b.x, a.y+b.y, a.z+b.z}; }
GM_INLINE gm_vec3d gm_sub3d(gm_vec3d a, gm_vec3d b) { return (gm_vec3d){a.x-b.x, a.y-b.y, a.z-b.z}; }
GM_INLINE gm_vec3d gm_mul3d(gm_vec3d a, gm_vec3d b) { return (gm_vec3d){a.x*b.x, a.y*b.y, a.z*b.z}; }
GM_INLINE gm_vec3d gm_scale3d(gm_vec3d a, double s) { return (gm_vec3d){a.x*s, a.y*s, a.z*s}; }
GM_INLINE double   gm_dot3d(gm_vec3d a, gm_vec3d b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
GM_INLINE gm_vec3d gm_cross3d(gm_vec3d a, gm_vec3d b) {
    return (gm_vec3d){a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
GM_INLINE double   gm_len3d(gm_vec3d a) { return sqrt(gm_dot3d(a, a)); }
GM_INLINE gm_vec3d gm_normalize3d(gm_vec3d a) { double l = gm_len3d(a); return l > 0 ? gm_scale3d(a, 1.0/l) : a; }
GM_INLINE double   gm_dist3d(gm_vec3d a, gm_vec3d b) { return gm_len3d(gm_sub3d(a, b)); }
GM_INLINE gm_vec3d gm_lerp3d(gm_vec3d a, gm_vec3d b, double t) { return gm_add3d(gm_scale3d(a, 1.0-t), gm_scale3d(b, t)); }

// -- Matrix Identity ---------------------------------------------------------

GM_INLINE gm_mat3 gm_mat3_identity(void) {
    return (gm_mat3){{ 1,0,0, 0,1,0, 0,0,1 }};
}

GM_INLINE gm_mat4 gm_mat4_identity(void) {
    return (gm_mat4){{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }};
}

// -- Matrix Transpose --------------------------------------------------------

GM_INLINE gm_mat3 gm_mat3_transpose(gm_mat3 a) {
    return (gm_mat3){{
        a.m[0], a.m[3], a.m[6],
        a.m[1], a.m[4], a.m[7],
        a.m[2], a.m[5], a.m[8]
    }};
}

GM_INLINE gm_mat4 gm_mat4_transpose(gm_mat4 a) {
    return (gm_mat4){{
        a.m[0], a.m[4], a.m[8],  a.m[12],
        a.m[1], a.m[5], a.m[9],  a.m[13],
        a.m[2], a.m[6], a.m[10], a.m[14],
        a.m[3], a.m[7], a.m[11], a.m[15]
    }};
}

// -- Matrix Multiply ---------------------------------------------------------

GM_INLINE gm_mat3 gm_mat3_mul(gm_mat3 a, gm_mat3 b) {
    gm_mat3 r = {0};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                r.m[i*3+j] += a.m[i*3+k] * b.m[k*3+j];
    return r;
}

GM_INLINE gm_mat4 gm_mat4_mul(gm_mat4 a, gm_mat4 b) {
    gm_mat4 r = {0};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                r.m[i*4+j] += a.m[i*4+k] * b.m[k*4+j];
    return r;
}

// -- Matrix-Vector Multiply --------------------------------------------------

GM_INLINE gm_vec3d gm_mat3_mulv(gm_mat3 M, gm_vec3d v) {
    return (gm_vec3d){
        M.m[0]*v.x + M.m[1]*v.y + M.m[2]*v.z,
        M.m[3]*v.x + M.m[4]*v.y + M.m[5]*v.z,
        M.m[6]*v.x + M.m[7]*v.y + M.m[8]*v.z
    };
}

GM_INLINE gm_vec4f gm_mat4_mulv(gm_mat4 M, gm_vec4f v) {
    return (gm_vec4f){
        (float)(M.m[0]*v.x  + M.m[1]*v.y  + M.m[2]*v.z  + M.m[3]*v.w),
        (float)(M.m[4]*v.x  + M.m[5]*v.y  + M.m[6]*v.z  + M.m[7]*v.w),
        (float)(M.m[8]*v.x  + M.m[9]*v.y  + M.m[10]*v.z + M.m[11]*v.w),
        (float)(M.m[12]*v.x + M.m[13]*v.y + M.m[14]*v.z + M.m[15]*v.w)
    };
}

// -- Matrix Determinant (3x3) ------------------------------------------------

GM_INLINE double gm_mat3_det(gm_mat3 a) {
    return a.m[0]*(a.m[4]*a.m[8] - a.m[5]*a.m[7])
         - a.m[1]*(a.m[3]*a.m[8] - a.m[5]*a.m[6])
         + a.m[2]*(a.m[3]*a.m[7] - a.m[4]*a.m[6]);
}

// -- BBox3f Ops --------------------------------------------------------------

GM_INLINE gm_bbox3f gm_bbox3f_empty(void) {
    return (gm_bbox3f){
        { INFINITY,  INFINITY,  INFINITY},
        {-INFINITY, -INFINITY, -INFINITY}
    };
}

GM_INLINE gm_bbox3f gm_bbox3f_expand(gm_bbox3f b, gm_vec3f p) {
    if (p.x < b.lo.x) b.lo.x = p.x;
    if (p.y < b.lo.y) b.lo.y = p.y;
    if (p.z < b.lo.z) b.lo.z = p.z;
    if (p.x > b.hi.x) b.hi.x = p.x;
    if (p.y > b.hi.y) b.hi.y = p.y;
    if (p.z > b.hi.z) b.hi.z = p.z;
    return b;
}

GM_INLINE bool gm_bbox3f_contains(gm_bbox3f b, gm_vec3f p) {
    return p.x >= b.lo.x && p.x <= b.hi.x
        && p.y >= b.lo.y && p.y <= b.hi.y
        && p.z >= b.lo.z && p.z <= b.hi.z;
}

GM_INLINE bool gm_bbox3f_intersects(gm_bbox3f a, gm_bbox3f b) {
    return a.lo.x <= b.hi.x && a.hi.x >= b.lo.x
        && a.lo.y <= b.hi.y && a.hi.y >= b.lo.y
        && a.lo.z <= b.hi.z && a.hi.z >= b.lo.z;
}

GM_INLINE gm_bbox3f gm_bbox3f_union(gm_bbox3f a, gm_bbox3f b) {
    return (gm_bbox3f){
        { fminf(a.lo.x, b.lo.x), fminf(a.lo.y, b.lo.y), fminf(a.lo.z, b.lo.z) },
        { fmaxf(a.hi.x, b.hi.x), fmaxf(a.hi.y, b.hi.y), fmaxf(a.hi.z, b.hi.z) }
    };
}

GM_INLINE gm_vec3f gm_bbox3f_center(gm_bbox3f b) {
    return (gm_vec3f){
        0.5f*(b.lo.x + b.hi.x),
        0.5f*(b.lo.y + b.hi.y),
        0.5f*(b.lo.z + b.hi.z)
    };
}

GM_INLINE gm_vec3f gm_bbox3f_size(gm_bbox3f b) {
    return (gm_vec3f){ b.hi.x - b.lo.x, b.hi.y - b.lo.y, b.hi.z - b.lo.z };
}

GM_INLINE float gm_bbox3f_volume(gm_bbox3f b) {
    gm_vec3f s = gm_bbox3f_size(b);
    return s.x * s.y * s.z;
}

// -- Quaternion Inline Ops ---------------------------------------------------

GM_INLINE gm_quat gm_quat_identity(void) { return (gm_quat){1, 0, 0, 0}; }

GM_INLINE gm_quat gm_quat_from_axis_angle(gm_vec3d axis, double angle) {
    double half = angle * 0.5;
    double s = sin(half);
    gm_vec3d a = gm_normalize3d(axis);
    return (gm_quat){ cos(half), a.x*s, a.y*s, a.z*s };
}

GM_INLINE gm_quat gm_quat_mul(gm_quat a, gm_quat b) {
    return (gm_quat){
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
    };
}

GM_INLINE double gm_quat_norm(gm_quat q) {
    return sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
}

GM_INLINE gm_quat gm_quat_normalize(gm_quat q) {
    double n = gm_quat_norm(q);
    if (n < GM_EPS) return gm_quat_identity();
    double inv = 1.0 / n;
    return (gm_quat){ q.w*inv, q.x*inv, q.y*inv, q.z*inv };
}

GM_INLINE gm_vec3d gm_quat_rotate_vec3d(gm_quat q, gm_vec3d v) {
    // q * v * q^-1, optimized
    gm_vec3d u = {q.x, q.y, q.z};
    double s = q.w;
    gm_vec3d t = gm_scale3d(gm_cross3d(u, v), 2.0);
    return gm_add3d(gm_add3d(v, gm_scale3d(t, s)), gm_cross3d(u, t));
}

GM_INLINE gm_mat3 gm_quat_to_mat3(gm_quat q) {
    double xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
    double xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    double wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    return (gm_mat3){{
        1-2*(yy+zz), 2*(xy-wz),   2*(xz+wy),
        2*(xy+wz),   1-2*(xx+zz), 2*(yz-wx),
        2*(xz-wy),   2*(yz+wx),   1-2*(xx+yy)
    }};
}

// -- Non-inline Declarations -------------------------------------------------

GM_NODISCARD GMDEF gm_status gm_mat3_inv(gm_mat3 a, gm_mat3* out);
GM_NODISCARD GMDEF gm_status gm_mat4_inv(gm_mat4 a, gm_mat4* out);

GM_NODISCARD GMDEF gm_status gm_rodrigues(const gm_vec3d* rvec, gm_mat3* out);
GM_NODISCARD GMDEF gm_status gm_rodrigues_inv(const gm_mat3* R, gm_vec3d* out);

GMDEF gm_mat3 gm_rotation_x(double angle);
GMDEF gm_mat3 gm_rotation_y(double angle);
GMDEF gm_mat3 gm_rotation_z(double angle);
GMDEF gm_mat4 gm_translation(double tx, double ty, double tz);
GMDEF gm_mat4 gm_look_at(gm_vec3d eye, gm_vec3d target, gm_vec3d up);
GMDEF gm_mat4 gm_perspective(double fov, double aspect, double near_val, double far_val);

GMDEF gm_quat gm_quat_slerp(gm_quat a, gm_quat b, double t);
GMDEF gm_quat gm_quat_from_mat3(gm_mat3 M);

GM_NODISCARD GMDEF gm_status gm_svd3x3(const gm_mat3* A, gm_mat3* U, gm_vec3d* sigma, gm_mat3* V);
GM_NODISCARD GMDEF gm_status gm_estimate_affine3d(const gm_vec3d* src, const gm_vec3d* dst,
                                                    int n, gm_mat4* out);

#endif // GEOMETRY_H

// ============================================================================
// IMPLEMENTATION
// ============================================================================

#ifdef GM_IMPLEMENTATION

// -- Status / Version Strings ------------------------------------------------

GMDEF const char* gm_status_str(gm_status s) {
    switch (s) {
        case GM_OK:              return "ok";
        case GM_ERR_NULL_ARG:    return "null argument";
        case GM_ERR_SINGULAR:    return "singular matrix";
        case GM_ERR_CONVERGENCE: return "convergence failure";
        case GM_ERR_INVALID:     return "invalid parameter";
        case GM_ERR_ALLOC:       return "allocation failure";
        default:                 return "unknown error";
    }
}

GMDEF const char* gm_version_str(void) {
    // computed once, static buffer
    static char buf[32];
    if (buf[0] == '\0') {
        int maj = GM_VERSION_MAJOR, min = GM_VERSION_MINOR, pat = GM_VERSION_PATCH;
        // manual int-to-string to avoid snprintf dependency
        char* p = buf;
        // major
        if (maj >= 10) *p++ = '0' + (maj / 10);
        *p++ = '0' + (maj % 10);
        *p++ = '.';
        if (min >= 10) *p++ = '0' + (min / 10);
        *p++ = '0' + (min % 10);
        *p++ = '.';
        if (pat >= 10) *p++ = '0' + (pat / 10);
        *p++ = '0' + (pat % 10);
        *p = '\0';
    }
    return buf;
}

// -- Mat3 Inverse ------------------------------------------------------------

GM_NODISCARD GMDEF gm_status gm_mat3_inv(gm_mat3 a, gm_mat3* out) {
    if (!out) return GM_ERR_NULL_ARG;
    double det = gm_mat3_det(a);
    if (fabs(det) < GM_EPS) return GM_ERR_SINGULAR;
    double inv_det = 1.0 / det;
    out->m[0] = (a.m[4]*a.m[8] - a.m[5]*a.m[7]) * inv_det;
    out->m[1] = (a.m[2]*a.m[7] - a.m[1]*a.m[8]) * inv_det;
    out->m[2] = (a.m[1]*a.m[5] - a.m[2]*a.m[4]) * inv_det;
    out->m[3] = (a.m[5]*a.m[6] - a.m[3]*a.m[8]) * inv_det;
    out->m[4] = (a.m[0]*a.m[8] - a.m[2]*a.m[6]) * inv_det;
    out->m[5] = (a.m[2]*a.m[3] - a.m[0]*a.m[5]) * inv_det;
    out->m[6] = (a.m[3]*a.m[7] - a.m[4]*a.m[6]) * inv_det;
    out->m[7] = (a.m[1]*a.m[6] - a.m[0]*a.m[7]) * inv_det;
    out->m[8] = (a.m[0]*a.m[4] - a.m[1]*a.m[3]) * inv_det;
    return GM_OK;
}

// -- Mat4 Inverse (Cramer's rule) --------------------------------------------

GM_NODISCARD GMDEF gm_status gm_mat4_inv(gm_mat4 a, gm_mat4* out) {
    if (!out) return GM_ERR_NULL_ARG;
    const double* m = a.m;
    double t[6];
    t[0] = m[8]*m[13] - m[9]*m[12];
    t[1] = m[8]*m[14] - m[10]*m[12];
    t[2] = m[8]*m[15] - m[11]*m[12];
    t[3] = m[9]*m[14] - m[10]*m[13];
    t[4] = m[9]*m[15] - m[11]*m[13];
    t[5] = m[10]*m[15] - m[11]*m[14];

    double c[16];
    c[0]  =  m[5]*t[5] - m[6]*t[4] + m[7]*t[3];
    c[1]  = -m[4]*t[5] + m[6]*t[2] - m[7]*t[1];
    c[2]  =  m[4]*t[4] - m[5]*t[2] + m[7]*t[0];
    c[3]  = -m[4]*t[3] + m[5]*t[1] - m[6]*t[0];

    double det = m[0]*c[0] + m[1]*c[1] + m[2]*c[2] + m[3]*c[3];
    if (fabs(det) < GM_EPS) return GM_ERR_SINGULAR;

    c[4]  = -m[1]*t[5] + m[2]*t[4] - m[3]*t[3];
    c[5]  =  m[0]*t[5] - m[2]*t[2] + m[3]*t[1];
    c[6]  = -m[0]*t[4] + m[1]*t[2] - m[3]*t[0];
    c[7]  =  m[0]*t[3] - m[1]*t[1] + m[2]*t[0];

    t[0] = m[0]*m[5] - m[1]*m[4];
    t[1] = m[0]*m[6] - m[2]*m[4];
    t[2] = m[0]*m[7] - m[3]*m[4];
    t[3] = m[1]*m[6] - m[2]*m[5];
    t[4] = m[1]*m[7] - m[3]*m[5];
    t[5] = m[2]*m[7] - m[3]*m[6];

    c[8]  =  m[13]*t[5] - m[14]*t[4] + m[15]*t[3];
    c[9]  = -m[12]*t[5] + m[14]*t[2] - m[15]*t[1];
    c[10] =  m[12]*t[4] - m[13]*t[2] + m[15]*t[0];
    c[11] = -m[12]*t[3] + m[13]*t[1] - m[14]*t[0];
    c[12] = -m[9]*t[5]  + m[10]*t[4] - m[11]*t[3];
    c[13] =  m[8]*t[5]  - m[10]*t[2] + m[11]*t[1];
    c[14] = -m[8]*t[4]  + m[9]*t[2]  - m[11]*t[0];
    c[15] =  m[8]*t[3]  - m[9]*t[1]  + m[10]*t[0];

    double inv_det = 1.0 / det;
    // Note: cofactor matrix must be transposed for the inverse
    out->m[0]  = c[0]*inv_det;  out->m[1]  = c[4]*inv_det;
    out->m[2]  = c[8]*inv_det;  out->m[3]  = c[12]*inv_det;
    out->m[4]  = c[1]*inv_det;  out->m[5]  = c[5]*inv_det;
    out->m[6]  = c[9]*inv_det;  out->m[7]  = c[13]*inv_det;
    out->m[8]  = c[2]*inv_det;  out->m[9]  = c[6]*inv_det;
    out->m[10] = c[10]*inv_det; out->m[11] = c[14]*inv_det;
    out->m[12] = c[3]*inv_det;  out->m[13] = c[7]*inv_det;
    out->m[14] = c[11]*inv_det; out->m[15] = c[15]*inv_det;
    return GM_OK;
}

// -- Rodrigues ---------------------------------------------------------------

GM_NODISCARD GMDEF gm_status gm_rodrigues(const gm_vec3d* rvec, gm_mat3* out) {
    if (!rvec || !out) return GM_ERR_NULL_ARG;
    double angle = gm_len3d(*rvec);
    if (angle < GM_EPS) {
        *out = gm_mat3_identity();
        return GM_OK;
    }
    gm_vec3d k = gm_scale3d(*rvec, 1.0 / angle);
    double c = cos(angle), s = sin(angle), t = 1.0 - c;
    out->m[0] = c + k.x*k.x*t;
    out->m[1] = k.x*k.y*t - k.z*s;
    out->m[2] = k.x*k.z*t + k.y*s;
    out->m[3] = k.y*k.x*t + k.z*s;
    out->m[4] = c + k.y*k.y*t;
    out->m[5] = k.y*k.z*t - k.x*s;
    out->m[6] = k.z*k.x*t - k.y*s;
    out->m[7] = k.z*k.y*t + k.x*s;
    out->m[8] = c + k.z*k.z*t;
    return GM_OK;
}

GM_NODISCARD GMDEF gm_status gm_rodrigues_inv(const gm_mat3* R, gm_vec3d* out) {
    if (!R || !out) return GM_ERR_NULL_ARG;
    // Extract axis-angle from rotation matrix
    double trace = R->m[0] + R->m[4] + R->m[8];
    double cos_angle = (trace - 1.0) * 0.5;
    cos_angle = gm_clampd(cos_angle, -1.0, 1.0);
    double angle = acos(cos_angle);
    if (angle < GM_EPS) {
        *out = (gm_vec3d){0, 0, 0};
        return GM_OK;
    }
    if (fabs(angle - GM_PI) < 1e-6) {
        // angle ~= pi: find axis from R + I diagonal
        double rx = sqrt(fmax(0.0, (R->m[0] + 1.0) * 0.5));
        double ry = sqrt(fmax(0.0, (R->m[4] + 1.0) * 0.5));
        double rz = sqrt(fmax(0.0, (R->m[8] + 1.0) * 0.5));
        // fix signs using off-diagonal
        if (R->m[1] + R->m[3] < 0) ry = -ry;
        if (R->m[2] + R->m[6] < 0) rz = -rz;
        // Ensure consistent sign: largest component positive
        if (rx < ry && rx < rz) rx = -rx;
        *out = gm_scale3d((gm_vec3d){rx, ry, rz}, angle);
        return GM_OK;
    }
    double s = 1.0 / (2.0 * sin(angle));
    out->x = (R->m[7] - R->m[5]) * s * angle;
    out->y = (R->m[2] - R->m[6]) * s * angle;
    out->z = (R->m[3] - R->m[1]) * s * angle;
    return GM_OK;
}

// -- Rotation Matrices -------------------------------------------------------

GMDEF gm_mat3 gm_rotation_x(double angle) {
    double c = cos(angle), s = sin(angle);
    return (gm_mat3){{ 1,0,0, 0,c,-s, 0,s,c }};
}

GMDEF gm_mat3 gm_rotation_y(double angle) {
    double c = cos(angle), s = sin(angle);
    return (gm_mat3){{ c,0,s, 0,1,0, -s,0,c }};
}

GMDEF gm_mat3 gm_rotation_z(double angle) {
    double c = cos(angle), s = sin(angle);
    return (gm_mat3){{ c,-s,0, s,c,0, 0,0,1 }};
}

// -- Translation (4x4) ------------------------------------------------------

GMDEF gm_mat4 gm_translation(double tx, double ty, double tz) {
    return (gm_mat4){{ 1,0,0,tx, 0,1,0,ty, 0,0,1,tz, 0,0,0,1 }};
}

// -- Look-At View Matrix -----------------------------------------------------

GMDEF gm_mat4 gm_look_at(gm_vec3d eye, gm_vec3d target, gm_vec3d up) {
    gm_vec3d f = gm_normalize3d(gm_sub3d(target, eye));
    gm_vec3d r = gm_normalize3d(gm_cross3d(f, up));
    gm_vec3d u = gm_cross3d(r, f);
    return (gm_mat4){{
         r.x,  r.y,  r.z, -gm_dot3d(r, eye),
         u.x,  u.y,  u.z, -gm_dot3d(u, eye),
        -f.x, -f.y, -f.z,  gm_dot3d(f, eye),
         0,    0,    0,    1
    }};
}

// -- Perspective Projection --------------------------------------------------

GMDEF gm_mat4 gm_perspective(double fov, double aspect, double near_val, double far_val) {
    double f = 1.0 / tan(fov * 0.5);
    double nf = 1.0 / (near_val - far_val);
    return (gm_mat4){{
        f/aspect, 0,  0,                          0,
        0,        f,  0,                          0,
        0,        0,  (far_val+near_val)*nf,      2.0*far_val*near_val*nf,
        0,        0, -1,                          0
    }};
}

// -- Quaternion Slerp --------------------------------------------------------

GMDEF gm_quat gm_quat_slerp(gm_quat a, gm_quat b, double t) {
    double dot = a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z;
    // If dot < 0, negate one to take shorter path
    if (dot < 0.0) {
        b = (gm_quat){-b.w, -b.x, -b.y, -b.z};
        dot = -dot;
    }
    if (dot > 0.9995) {
        // Linear interpolation for very close quaternions
        gm_quat r = {
            a.w + t*(b.w - a.w),
            a.x + t*(b.x - a.x),
            a.y + t*(b.y - a.y),
            a.z + t*(b.z - a.z)
        };
        return gm_quat_normalize(r);
    }
    double theta = acos(gm_clampd(dot, -1.0, 1.0));
    double sin_theta = sin(theta);
    double wa = sin((1.0 - t) * theta) / sin_theta;
    double wb = sin(t * theta) / sin_theta;
    return (gm_quat){
        wa*a.w + wb*b.w,
        wa*a.x + wb*b.x,
        wa*a.y + wb*b.y,
        wa*a.z + wb*b.z
    };
}

// -- Quaternion from Rotation Matrix -----------------------------------------

GMDEF gm_quat gm_quat_from_mat3(gm_mat3 M) {
    double trace = M.m[0] + M.m[4] + M.m[8];
    gm_quat q;
    if (trace > 0) {
        double s = 0.5 / sqrt(trace + 1.0);
        q.w = 0.25 / s;
        q.x = (M.m[7] - M.m[5]) * s;
        q.y = (M.m[2] - M.m[6]) * s;
        q.z = (M.m[3] - M.m[1]) * s;
    } else if (M.m[0] > M.m[4] && M.m[0] > M.m[8]) {
        double s = 2.0 * sqrt(1.0 + M.m[0] - M.m[4] - M.m[8]);
        q.w = (M.m[7] - M.m[5]) / s;
        q.x = 0.25 * s;
        q.y = (M.m[1] + M.m[3]) / s;
        q.z = (M.m[2] + M.m[6]) / s;
    } else if (M.m[4] > M.m[8]) {
        double s = 2.0 * sqrt(1.0 + M.m[4] - M.m[0] - M.m[8]);
        q.w = (M.m[2] - M.m[6]) / s;
        q.x = (M.m[1] + M.m[3]) / s;
        q.y = 0.25 * s;
        q.z = (M.m[5] + M.m[7]) / s;
    } else {
        double s = 2.0 * sqrt(1.0 + M.m[8] - M.m[0] - M.m[4]);
        q.w = (M.m[3] - M.m[1]) / s;
        q.x = (M.m[2] + M.m[6]) / s;
        q.y = (M.m[5] + M.m[7]) / s;
        q.z = 0.25 * s;
    }
    return gm_quat_normalize(q);
}

// -- SVD 3x3 (Jacobi one-sided) ---------------------------------------------

// Internal: compute 2x2 Jacobi rotation for symmetric matrix A^T*A
static void gm__jacobi_rotation(double app, double apq, double aqq,
                                 double* cs, double* sn) {
    if (fabs(apq) < GM_EPS) {
        *cs = 1.0; *sn = 0.0;
        return;
    }
    double tau = (aqq - app) / (2.0 * apq);
    double t;
    if (tau >= 0)
        t =  1.0 / ( tau + sqrt(1.0 + tau*tau));
    else
        t = -1.0 / (-tau + sqrt(1.0 + tau*tau));
    *cs = 1.0 / sqrt(1.0 + t*t);
    *sn = t * (*cs);
}

GM_NODISCARD GMDEF gm_status gm_svd3x3(const gm_mat3* A, gm_mat3* U,
                                         gm_vec3d* sigma, gm_mat3* V) {
    if (!A || !U || !sigma || !V) return GM_ERR_NULL_ARG;

    // Copy A into working matrix (stored in U)
    memcpy(U->m, A->m, sizeof(double)*9);
    *V = gm_mat3_identity();

    // One-sided Jacobi: apply rotations on the right to orthogonalize columns
    for (int iter = 0; iter < 100; iter++) {
        double off = 0;
        // For each pair of columns (p,q), compute dot and apply rotation
        for (int p = 0; p < 3; p++) {
            for (int q = p+1; q < 3; q++) {
                // Compute A^T*A elements for columns p,q
                double app = 0, aqq = 0, apq = 0;
                for (int i = 0; i < 3; i++) {
                    app += U->m[i*3+p] * U->m[i*3+p];
                    aqq += U->m[i*3+q] * U->m[i*3+q];
                    apq += U->m[i*3+p] * U->m[i*3+q];
                }
                off += apq*apq;
                double cs, sn;
                gm__jacobi_rotation(app, apq, aqq, &cs, &sn);
                // Apply rotation to columns of U and V
                for (int i = 0; i < 3; i++) {
                    double up = U->m[i*3+p], uq = U->m[i*3+q];
                    U->m[i*3+p] = cs*up - sn*uq;
                    U->m[i*3+q] = sn*up + cs*uq;
                    double vp = V->m[i*3+p], vq = V->m[i*3+q];
                    V->m[i*3+p] = cs*vp - sn*vq;
                    V->m[i*3+q] = sn*vp + cs*vq;
                }
            }
        }
        if (off < GM_EPS * GM_EPS) break;
    }

    // Extract singular values (column norms of U) and normalize U columns
    for (int j = 0; j < 3; j++) {
        double s = 0;
        for (int i = 0; i < 3; i++) s += U->m[i*3+j] * U->m[i*3+j];
        s = sqrt(s);
        ((double*)sigma)[j] = s;
        if (s > GM_EPS) {
            for (int i = 0; i < 3; i++) U->m[i*3+j] /= s;
        }
    }
    return GM_OK;
}

// -- Affine 3D Estimation (least-squares) ------------------------------------

GM_NODISCARD GMDEF gm_status gm_estimate_affine3d(const gm_vec3d* src,
                                                    const gm_vec3d* dst,
                                                    int n, gm_mat4* out) {
    if (!src || !dst || !out) return GM_ERR_NULL_ARG;
    if (n < 4) return GM_ERR_INVALID;

    // Solve for 3x4 affine matrix [R|t] that maps src -> dst
    // using normal equations: (A^T A) x = A^T b for each output dimension
    // A is n x 4 matrix [sx sy sz 1], b is the corresponding dst coordinate

    // Build A^T*A (4x4 symmetric) and A^T*b (4x3)
    double ATA[16] = {0};
    double ATb[12] = {0}; // 4 rows x 3 cols (for x,y,z of dst)

    for (int i = 0; i < n; i++) {
        double r[4] = { src[i].x, src[i].y, src[i].z, 1.0 };
        double d[3] = { dst[i].x, dst[i].y, dst[i].z };
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++)
                ATA[j*4+k] += r[j] * r[k];
            for (int k = 0; k < 3; k++)
                ATb[j*3+k] += r[j] * d[k];
        }
    }

    // Solve 4x4 system using Gauss elimination with partial pivoting
    // Augment ATA with ATb -> 4x7 matrix
    double aug[4][7];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) aug[i][j] = ATA[i*4+j];
        for (int j = 0; j < 3; j++) aug[i][4+j] = ATb[i*3+j];
    }

    for (int col = 0; col < 4; col++) {
        // Partial pivot
        int pivot = col;
        for (int row = col+1; row < 4; row++)
            if (fabs(aug[row][col]) > fabs(aug[pivot][col])) pivot = row;
        if (fabs(aug[pivot][col]) < GM_EPS) return GM_ERR_SINGULAR;
        if (pivot != col) {
            for (int j = 0; j < 7; j++) {
                double tmp = aug[col][j];
                aug[col][j] = aug[pivot][j];
                aug[pivot][j] = tmp;
            }
        }
        double diag = aug[col][col];
        for (int j = col; j < 7; j++) aug[col][j] /= diag;
        for (int row = 0; row < 4; row++) {
            if (row == col) continue;
            double f = aug[row][col];
            for (int j = col; j < 7; j++) aug[row][j] -= f * aug[col][j];
        }
    }

    // Extract result into 4x4 matrix (last row = [0 0 0 1])
    // Solution columns in aug[i][4..6] give rows of the 3x4 affine part
    *out = gm_mat4_identity();
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            // aug[i][4+j] is the coefficient for input dim i, output dim j
            // In row-major 4x4: row j (output), col i (input)
            out->m[j*4+i] = aug[i][4+j];
        }
    }
    // Row 3 stays [0 0 0 1]
    return GM_OK;
}

#endif // GM_IMPLEMENTATION
