// imgproc.h — Pure C23 single-header image processing library
// Single-header: declare API, then #define IP_IMPLEMENTATION in one .c file.
// Vesuvius Challenge / Villa Volume Cartographer.
#ifndef IMGPROC_H
#define IMGPROC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ── Version ─────────────────────────────────────────────────────────────────

#define IP_VERSION_MAJOR 0
#define IP_VERSION_MINOR 1
#define IP_VERSION_PATCH 0

// ── C23 Compat ──────────────────────────────────────────────────────────────

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define IP_NODISCARD    [[nodiscard]]
  #define IP_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define IP_NODISCARD    __attribute__((warn_unused_result))
  #define IP_MAYBE_UNUSED __attribute__((unused))
#else
  #define IP_NODISCARD
  #define IP_MAYBE_UNUSED
#endif

// ── Linkage ─────────────────────────────────────────────────────────────────

#ifndef IPDEF
  #ifdef IP_STATIC
    #define IPDEF static
  #else
    #define IPDEF extern
  #endif
#endif

// ── Allocator Hooks ─────────────────────────────────────────────────────────

#ifndef IP_MALLOC
  #include <stdlib.h>
  #define IP_MALLOC(sz)       malloc(sz)
  #define IP_REALLOC(p, sz)   realloc(p, sz)
  #define IP_FREE(p)          free(p)
  #define IP_CALLOC(n, sz)    calloc(n, sz)
#endif

// ── Constants & Flags ───────────────────────────────────────────────────────

#define IP_FLAG_OWNS_DATA (1u << 0)

// ── Status Codes ────────────────────────────────────────────────────────────

typedef enum ip_status {
    IP_OK = 0,
    IP_ERR_NULL_ARG,
    IP_ERR_ALLOC,
    IP_ERR_INVALID,       // invalid parameter (dimensions, kernel size, etc.)
    IP_ERR_DTYPE,         // unsupported or mismatched dtype
    IP_ERR_BOUNDS,        // index out of range
    IP_ERR_UNSUPPORTED,   // unimplemented operation
} ip_status;

// ── Data Types ──────────────────────────────────────────────────────────────

typedef enum ip_dtype {
    IP_U8 = 0,
    IP_U16,
    IP_F32,
    IP_I32,
    IP_DTYPE_COUNT,
} ip_dtype;

// ── Interpolation ──────────────────────────────────────────────────────────

typedef enum ip_interp {
    IP_NEAREST = 0,
    IP_BILINEAR,
} ip_interp;

// ── Color Conversion Codes ─────────────────────────────────────────────────

typedef enum ip_color_cvt {
    IP_GRAY2RGB = 0,
    IP_RGB2GRAY,
    IP_RGB2BGR,
    IP_BGR2RGB,
} ip_color_cvt;

// ── Threshold Types ────────────────────────────────────────────────────────

typedef enum ip_thresh_type {
    IP_THRESH_BINARY = 0,
    IP_THRESH_BINARY_INV,
    IP_THRESH_OTSU,
} ip_thresh_type;

// ── Image ──────────────────────────────────────────────────────────────────

typedef struct ip_image {
    void*    data;
    int      width;
    int      height;
    int      channels;  // 1 (gray), 3 (RGB), 4 (RGBA)
    ip_dtype dtype;
    uint32_t flags;     // IP_FLAG_OWNS_DATA
} ip_image;

// ── Dtype Helpers ──────────────────────────────────────────────────────────

IPDEF size_t      ip_dtype_size(ip_dtype d);
IPDEF const char* ip_status_str(ip_status s);
IPDEF const char* ip_version_str(void);

// ── Lifecycle ──────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_create(int w, int h, int channels,
                                       ip_dtype dtype, ip_image** out);
IP_NODISCARD IPDEF ip_status ip_from_data(void* data, int w, int h,
                                          int channels, ip_dtype dtype,
                                          ip_image** out);
IP_NODISCARD IPDEF ip_status ip_clone(ip_image** out, const ip_image* src);
IPDEF void ip_free(ip_image* img);

// ── Pixel Access ───────────────────────────────────────────────────────────

IPDEF void*    ip_ptr(const ip_image* img, int x, int y);

IPDEF uint8_t  ip_get_u8(const ip_image* img, int x, int y, int channel);
IPDEF void     ip_set_u8(ip_image* img, int x, int y, int channel, uint8_t val);

IPDEF uint16_t ip_get_u16(const ip_image* img, int x, int y, int channel);
IPDEF void     ip_set_u16(ip_image* img, int x, int y, int channel, uint16_t val);

IPDEF float    ip_get_f32(const ip_image* img, int x, int y, int channel);
IPDEF void     ip_set_f32(ip_image* img, int x, int y, int channel, float val);

IPDEF int32_t  ip_get_i32(const ip_image* img, int x, int y, int channel);
IPDEF void     ip_set_i32(ip_image* img, int x, int y, int channel, int32_t val);

// ── Resize ─────────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_resize(ip_image** out, const ip_image* src,
                                       int new_w, int new_h, ip_interp method);

// ── Color Conversion ──────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_cvt_color(ip_image** out, const ip_image* src,
                                          ip_color_cvt code);

// ── Filtering ──────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_gaussian_blur(ip_image** out, const ip_image* src,
                                              int ksize, double sigma);
IP_NODISCARD IPDEF ip_status ip_box_blur(ip_image** out, const ip_image* src,
                                         int ksize);
IP_NODISCARD IPDEF ip_status ip_median_blur(ip_image** out, const ip_image* src,
                                            int ksize);

// ── Morphology ─────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_dilate(ip_image** out, const ip_image* src,
                                       int ksize);
IP_NODISCARD IPDEF ip_status ip_erode(ip_image** out, const ip_image* src,
                                      int ksize);

// ── Thresholding ───────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_threshold(ip_image** out, const ip_image* src,
                                          double thresh, double maxval,
                                          ip_thresh_type type);

// ── Connected Components ───────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_connected_components(const ip_image* binary,
                                                     ip_image** labels,
                                                     int* count);

// ── Geometric Transforms ──────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_flip(ip_image** out, const ip_image* src,
                                     int flip_code);
IP_NODISCARD IPDEF ip_status ip_rotate90(ip_image** out, const ip_image* src,
                                         int times);

// ── Statistics ─────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_min_max(const ip_image* img,
                                        double* min_val, double* max_val);
IP_NODISCARD IPDEF ip_status ip_normalize(ip_image** out, const ip_image* src,
                                          double alpha, double beta);

// ── Drawing ────────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_fill(ip_image* img, double value);
IP_NODISCARD IPDEF ip_status ip_draw_rect(ip_image* img, int x, int y,
                                          int w, int h, double value);

// ════════════════════════════════════════════════════════════════════════════
// ██ Implementation
// ════════════════════════════════════════════════════════════════════════════

#ifdef IP_IMPLEMENTATION

#include <math.h>
#include <float.h>

// ── Internal Helpers ───────────────────────────────────────────────────────

static size_t ip__dtype_size_tbl[] = {
    [IP_U8]  = 1,
    [IP_U16] = 2,
    [IP_F32] = 4,
    [IP_I32] = 4,
};

static inline size_t ip__pixel_size(const ip_image* img) {
    return ip__dtype_size_tbl[img->dtype] * (size_t)img->channels;
}

static inline size_t ip__row_stride(const ip_image* img) {
    return ip__pixel_size(img) * (size_t)img->width;
}

static inline size_t ip__data_size(const ip_image* img) {
    return ip__row_stride(img) * (size_t)img->height;
}

// Get pixel value as double regardless of dtype
static inline double ip__get_pixel(const ip_image* img, int x, int y, int c) {
    const uint8_t* base = (const uint8_t*)img->data
        + ((size_t)y * img->width + x) * ip__dtype_size_tbl[img->dtype] * img->channels
        + (size_t)c * ip__dtype_size_tbl[img->dtype];
    switch (img->dtype) {
        case IP_U8:  return (double)(*(const uint8_t*)base);
        case IP_U16: return (double)(*(const uint16_t*)base);
        case IP_F32: return (double)(*(const float*)base);
        case IP_I32: return (double)(*(const int32_t*)base);
        default: return 0.0;
    }
}

// Set pixel value from double regardless of dtype
static inline void ip__set_pixel(ip_image* img, int x, int y, int c, double val) {
    uint8_t* base = (uint8_t*)img->data
        + ((size_t)y * img->width + x) * ip__dtype_size_tbl[img->dtype] * img->channels
        + (size_t)c * ip__dtype_size_tbl[img->dtype];
    switch (img->dtype) {
        case IP_U8:  *(uint8_t*)base  = (uint8_t)(val < 0 ? 0 : val > 255 ? 255 : val);   break;
        case IP_U16: *(uint16_t*)base = (uint16_t)(val < 0 ? 0 : val > 65535 ? 65535 : val); break;
        case IP_F32: *(float*)base    = (float)val; break;
        case IP_I32: *(int32_t*)base  = (int32_t)val; break;
        default: break;
    }
}

// ── Dtype Helpers ──────────────────────────────────────────────────────────

IPDEF size_t ip_dtype_size(ip_dtype d) {
    if (d < 0 || d >= IP_DTYPE_COUNT) return 0;
    return ip__dtype_size_tbl[d];
}

IPDEF const char* ip_status_str(ip_status s) {
    switch (s) {
        case IP_OK:             return "OK";
        case IP_ERR_NULL_ARG:   return "null argument";
        case IP_ERR_ALLOC:      return "allocation failed";
        case IP_ERR_INVALID:    return "invalid parameter";
        case IP_ERR_DTYPE:      return "unsupported dtype";
        case IP_ERR_BOUNDS:     return "index out of bounds";
        case IP_ERR_UNSUPPORTED:return "unsupported operation";
    }
    return "unknown error";
}

IPDEF const char* ip_version_str(void) {
    return "0.1.0";
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_create(int w, int h, int channels,
                                       ip_dtype dtype, ip_image** out) {
    if (!out) return IP_ERR_NULL_ARG;
    if (w <= 0 || h <= 0 || channels <= 0 || channels > 4)
        return IP_ERR_INVALID;
    if (dtype < 0 || dtype >= IP_DTYPE_COUNT)
        return IP_ERR_DTYPE;

    ip_image* img = (ip_image*)IP_MALLOC(sizeof(ip_image));
    if (!img) return IP_ERR_ALLOC;

    size_t dsz = ip__dtype_size_tbl[dtype];
    size_t total = (size_t)w * h * channels * dsz;
    img->data = IP_CALLOC(1, total);
    if (!img->data) { IP_FREE(img); return IP_ERR_ALLOC; }

    img->width    = w;
    img->height   = h;
    img->channels = channels;
    img->dtype    = dtype;
    img->flags    = IP_FLAG_OWNS_DATA;

    *out = img;
    return IP_OK;
}

IP_NODISCARD IPDEF ip_status ip_from_data(void* data, int w, int h,
                                          int channels, ip_dtype dtype,
                                          ip_image** out) {
    if (!out || !data) return IP_ERR_NULL_ARG;
    if (w <= 0 || h <= 0 || channels <= 0 || channels > 4)
        return IP_ERR_INVALID;
    if (dtype < 0 || dtype >= IP_DTYPE_COUNT)
        return IP_ERR_DTYPE;

    ip_image* img = (ip_image*)IP_MALLOC(sizeof(ip_image));
    if (!img) return IP_ERR_ALLOC;

    img->data     = data;
    img->width    = w;
    img->height   = h;
    img->channels = channels;
    img->dtype    = dtype;
    img->flags    = 0; // does NOT own data

    *out = img;
    return IP_OK;
}

IP_NODISCARD IPDEF ip_status ip_clone(ip_image** out, const ip_image* src) {
    if (!out || !src) return IP_ERR_NULL_ARG;

    ip_image* img = (ip_image*)IP_MALLOC(sizeof(ip_image));
    if (!img) return IP_ERR_ALLOC;

    size_t total = ip__data_size(src);
    img->data = IP_MALLOC(total);
    if (!img->data) { IP_FREE(img); return IP_ERR_ALLOC; }

    memcpy(img->data, src->data, total);
    img->width    = src->width;
    img->height   = src->height;
    img->channels = src->channels;
    img->dtype    = src->dtype;
    img->flags    = IP_FLAG_OWNS_DATA;

    *out = img;
    return IP_OK;
}

IPDEF void ip_free(ip_image* img) {
    if (!img) return;
    if (img->flags & IP_FLAG_OWNS_DATA) {
        IP_FREE(img->data);
    }
    IP_FREE(img);
}

// ── Pixel Access ───────────────────────────────────────────────────────────

IPDEF void* ip_ptr(const ip_image* img, int x, int y) {
    if (!img || !img->data) return NULL;
    size_t off = ((size_t)y * img->width + x) * ip__pixel_size(img);
    return (uint8_t*)img->data + off;
}

IPDEF uint8_t ip_get_u8(const ip_image* img, int x, int y, int channel) {
    const uint8_t* p = (const uint8_t*)ip_ptr(img, x, y);
    return p[channel];
}

IPDEF void ip_set_u8(ip_image* img, int x, int y, int channel, uint8_t val) {
    uint8_t* p = (uint8_t*)ip_ptr(img, x, y);
    p[channel] = val;
}

IPDEF uint16_t ip_get_u16(const ip_image* img, int x, int y, int channel) {
    const uint16_t* p = (const uint16_t*)ip_ptr(img, x, y);
    return p[channel];
}

IPDEF void ip_set_u16(ip_image* img, int x, int y, int channel, uint16_t val) {
    uint16_t* p = (uint16_t*)ip_ptr(img, x, y);
    p[channel] = val;
}

IPDEF float ip_get_f32(const ip_image* img, int x, int y, int channel) {
    const float* p = (const float*)ip_ptr(img, x, y);
    return p[channel];
}

IPDEF void ip_set_f32(ip_image* img, int x, int y, int channel, float val) {
    float* p = (float*)ip_ptr(img, x, y);
    p[channel] = val;
}

IPDEF int32_t ip_get_i32(const ip_image* img, int x, int y, int channel) {
    const int32_t* p = (const int32_t*)ip_ptr(img, x, y);
    return p[channel];
}

IPDEF void ip_set_i32(ip_image* img, int x, int y, int channel, int32_t val) {
    int32_t* p = (int32_t*)ip_ptr(img, x, y);
    p[channel] = val;
}

// ── Resize ─────────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_resize(ip_image** out, const ip_image* src,
                                       int new_w, int new_h, ip_interp method) {
    if (!out || !src) return IP_ERR_NULL_ARG;
    if (new_w <= 0 || new_h <= 0) return IP_ERR_INVALID;

    ip_image* dst = NULL;
    ip_status s = ip_create(new_w, new_h, src->channels, src->dtype, &dst);
    if (s != IP_OK) return s;

    double sx = (double)src->width  / new_w;
    double sy = (double)src->height / new_h;

    for (int y = 0; y < new_h; y++) {
        for (int x = 0; x < new_w; x++) {
            double fx = (x + 0.5) * sx - 0.5;
            double fy = (y + 0.5) * sy - 0.5;

            if (method == IP_NEAREST) {
                int ix = (int)(fx + 0.5);
                int iy = (int)(fy + 0.5);
                if (ix < 0) ix = 0;
                if (iy < 0) iy = 0;
                if (ix >= src->width)  ix = src->width  - 1;
                if (iy >= src->height) iy = src->height - 1;
                for (int c = 0; c < src->channels; c++) {
                    ip__set_pixel(dst, x, y, c, ip__get_pixel(src, ix, iy, c));
                }
            } else { // IP_BILINEAR
                int x0 = (int)floor(fx);
                int y0 = (int)floor(fy);
                int x1 = x0 + 1;
                int y1 = y0 + 1;
                double xf = fx - x0;
                double yf = fy - y0;

                if (x0 < 0) x0 = 0;
                if (y0 < 0) y0 = 0;
                if (x1 >= src->width)  x1 = src->width  - 1;
                if (y1 >= src->height) y1 = src->height - 1;
                if (x0 >= src->width)  x0 = src->width  - 1;
                if (y0 >= src->height) y0 = src->height - 1;

                for (int c = 0; c < src->channels; c++) {
                    double v00 = ip__get_pixel(src, x0, y0, c);
                    double v10 = ip__get_pixel(src, x1, y0, c);
                    double v01 = ip__get_pixel(src, x0, y1, c);
                    double v11 = ip__get_pixel(src, x1, y1, c);
                    double v = v00 * (1 - xf) * (1 - yf) +
                               v10 * xf * (1 - yf) +
                               v01 * (1 - xf) * yf +
                               v11 * xf * yf;
                    ip__set_pixel(dst, x, y, c, v);
                }
            }
        }
    }

    *out = dst;
    return IP_OK;
}

// ── Color Conversion ──────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_cvt_color(ip_image** out, const ip_image* src,
                                          ip_color_cvt code) {
    if (!out || !src) return IP_ERR_NULL_ARG;

    switch (code) {
    case IP_RGB2GRAY: {
        if (src->channels < 3) return IP_ERR_INVALID;
        ip_image* dst = NULL;
        ip_status s = ip_create(src->width, src->height, 1, src->dtype, &dst);
        if (s != IP_OK) return s;

        for (int y = 0; y < src->height; y++) {
            for (int x = 0; x < src->width; x++) {
                double r = ip__get_pixel(src, x, y, 0);
                double g = ip__get_pixel(src, x, y, 1);
                double b = ip__get_pixel(src, x, y, 2);
                double gray = 0.299 * r + 0.587 * g + 0.114 * b;
                ip__set_pixel(dst, x, y, 0, gray);
            }
        }
        *out = dst;
        return IP_OK;
    }
    case IP_GRAY2RGB: {
        if (src->channels != 1) return IP_ERR_INVALID;
        ip_image* dst = NULL;
        ip_status s = ip_create(src->width, src->height, 3, src->dtype, &dst);
        if (s != IP_OK) return s;

        for (int y = 0; y < src->height; y++) {
            for (int x = 0; x < src->width; x++) {
                double v = ip__get_pixel(src, x, y, 0);
                ip__set_pixel(dst, x, y, 0, v);
                ip__set_pixel(dst, x, y, 1, v);
                ip__set_pixel(dst, x, y, 2, v);
            }
        }
        *out = dst;
        return IP_OK;
    }
    case IP_RGB2BGR:
    case IP_BGR2RGB: {
        if (src->channels < 3) return IP_ERR_INVALID;
        ip_image* dst = NULL;
        ip_status s = ip_create(src->width, src->height, src->channels, src->dtype, &dst);
        if (s != IP_OK) return s;

        for (int y = 0; y < src->height; y++) {
            for (int x = 0; x < src->width; x++) {
                ip__set_pixel(dst, x, y, 0, ip__get_pixel(src, x, y, 2));
                ip__set_pixel(dst, x, y, 1, ip__get_pixel(src, x, y, 1));
                ip__set_pixel(dst, x, y, 2, ip__get_pixel(src, x, y, 0));
                for (int c = 3; c < src->channels; c++) {
                    ip__set_pixel(dst, x, y, c, ip__get_pixel(src, x, y, c));
                }
            }
        }
        *out = dst;
        return IP_OK;
    }
    }
    return IP_ERR_UNSUPPORTED;
}

// ── Gaussian Blur (separable) ──────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_gaussian_blur(ip_image** out, const ip_image* src,
                                              int ksize, double sigma) {
    if (!out || !src) return IP_ERR_NULL_ARG;
    if (ksize <= 0 || ksize % 2 == 0) return IP_ERR_INVALID;
    if (ksize == 1) return ip_clone(out, src);

    int radius = ksize / 2;

    // Build 1D kernel
    if (sigma <= 0) sigma = 0.3 * ((ksize - 1) * 0.5 - 1) + 0.8;
    double* kernel = (double*)IP_MALLOC(sizeof(double) * ksize);
    if (!kernel) return IP_ERR_ALLOC;

    double sum = 0;
    for (int i = 0; i < ksize; i++) {
        double d = i - radius;
        kernel[i] = exp(-0.5 * d * d / (sigma * sigma));
        sum += kernel[i];
    }
    for (int i = 0; i < ksize; i++) kernel[i] /= sum;

    // Temporary image for horizontal pass
    ip_image* tmp = NULL;
    ip_status s = ip_create(src->width, src->height, src->channels, src->dtype, &tmp);
    if (s != IP_OK) { IP_FREE(kernel); return s; }

    // Horizontal pass
    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            for (int c = 0; c < src->channels; c++) {
                double acc = 0;
                for (int k = -radius; k <= radius; k++) {
                    int sx = x + k;
                    if (sx < 0) sx = 0;
                    if (sx >= src->width) sx = src->width - 1;
                    acc += ip__get_pixel(src, sx, y, c) * kernel[k + radius];
                }
                ip__set_pixel(tmp, x, y, c, acc);
            }
        }
    }

    // Output for vertical pass
    ip_image* dst = NULL;
    s = ip_create(src->width, src->height, src->channels, src->dtype, &dst);
    if (s != IP_OK) { ip_free(tmp); IP_FREE(kernel); return s; }

    // Vertical pass
    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            for (int c = 0; c < src->channels; c++) {
                double acc = 0;
                for (int k = -radius; k <= radius; k++) {
                    int sy = y + k;
                    if (sy < 0) sy = 0;
                    if (sy >= src->height) sy = src->height - 1;
                    acc += ip__get_pixel(tmp, x, sy, c) * kernel[k + radius];
                }
                ip__set_pixel(dst, x, y, c, acc);
            }
        }
    }

    ip_free(tmp);
    IP_FREE(kernel);
    *out = dst;
    return IP_OK;
}

// ── Box Blur ───────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_box_blur(ip_image** out, const ip_image* src,
                                         int ksize) {
    if (!out || !src) return IP_ERR_NULL_ARG;
    if (ksize <= 0 || ksize % 2 == 0) return IP_ERR_INVALID;
    if (ksize == 1) return ip_clone(out, src);

    int radius = ksize / 2;
    double inv = 1.0 / (ksize * ksize);

    ip_image* dst = NULL;
    ip_status s = ip_create(src->width, src->height, src->channels, src->dtype, &dst);
    if (s != IP_OK) return s;

    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            for (int c = 0; c < src->channels; c++) {
                double acc = 0;
                for (int ky = -radius; ky <= radius; ky++) {
                    int sy = y + ky;
                    if (sy < 0) sy = 0;
                    if (sy >= src->height) sy = src->height - 1;
                    for (int kx = -radius; kx <= radius; kx++) {
                        int sx = x + kx;
                        if (sx < 0) sx = 0;
                        if (sx >= src->width) sx = src->width - 1;
                        acc += ip__get_pixel(src, sx, sy, c);
                    }
                }
                ip__set_pixel(dst, x, y, c, acc * inv);
            }
        }
    }

    *out = dst;
    return IP_OK;
}

// ── Median Blur ────────────────────────────────────────────────────────────

// Comparison for qsort
static int ip__cmp_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

IP_NODISCARD IPDEF ip_status ip_median_blur(ip_image** out, const ip_image* src,
                                            int ksize) {
    if (!out || !src) return IP_ERR_NULL_ARG;
    if (ksize <= 0 || ksize % 2 == 0) return IP_ERR_INVALID;
    if (ksize == 1) return ip_clone(out, src);

    int radius = ksize / 2;
    int ksq = ksize * ksize;

    double* buf = (double*)IP_MALLOC(sizeof(double) * ksq);
    if (!buf) return IP_ERR_ALLOC;

    ip_image* dst = NULL;
    ip_status s = ip_create(src->width, src->height, src->channels, src->dtype, &dst);
    if (s != IP_OK) { IP_FREE(buf); return s; }

    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            for (int c = 0; c < src->channels; c++) {
                int n = 0;
                for (int ky = -radius; ky <= radius; ky++) {
                    int sy = y + ky;
                    if (sy < 0) sy = 0;
                    if (sy >= src->height) sy = src->height - 1;
                    for (int kx = -radius; kx <= radius; kx++) {
                        int sx = x + kx;
                        if (sx < 0) sx = 0;
                        if (sx >= src->width) sx = src->width - 1;
                        buf[n++] = ip__get_pixel(src, sx, sy, c);
                    }
                }
                qsort(buf, n, sizeof(double), ip__cmp_double);
                ip__set_pixel(dst, x, y, c, buf[n / 2]);
            }
        }
    }

    IP_FREE(buf);
    *out = dst;
    return IP_OK;
}

// ── Morphology ─────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_dilate(ip_image** out, const ip_image* src,
                                       int ksize) {
    if (!out || !src) return IP_ERR_NULL_ARG;
    if (ksize <= 0 || ksize % 2 == 0) return IP_ERR_INVALID;

    int radius = ksize / 2;
    ip_image* dst = NULL;
    ip_status s = ip_create(src->width, src->height, src->channels, src->dtype, &dst);
    if (s != IP_OK) return s;

    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            for (int c = 0; c < src->channels; c++) {
                double maxv = -DBL_MAX;
                for (int ky = -radius; ky <= radius; ky++) {
                    int sy = y + ky;
                    if (sy < 0 || sy >= src->height) continue;
                    for (int kx = -radius; kx <= radius; kx++) {
                        int sx = x + kx;
                        if (sx < 0 || sx >= src->width) continue;
                        double v = ip__get_pixel(src, sx, sy, c);
                        if (v > maxv) maxv = v;
                    }
                }
                ip__set_pixel(dst, x, y, c, maxv);
            }
        }
    }

    *out = dst;
    return IP_OK;
}

IP_NODISCARD IPDEF ip_status ip_erode(ip_image** out, const ip_image* src,
                                      int ksize) {
    if (!out || !src) return IP_ERR_NULL_ARG;
    if (ksize <= 0 || ksize % 2 == 0) return IP_ERR_INVALID;

    int radius = ksize / 2;
    ip_image* dst = NULL;
    ip_status s = ip_create(src->width, src->height, src->channels, src->dtype, &dst);
    if (s != IP_OK) return s;

    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            for (int c = 0; c < src->channels; c++) {
                double minv = DBL_MAX;
                for (int ky = -radius; ky <= radius; ky++) {
                    int sy = y + ky;
                    if (sy < 0 || sy >= src->height) continue;
                    for (int kx = -radius; kx <= radius; kx++) {
                        int sx = x + kx;
                        if (sx < 0 || sx >= src->width) continue;
                        double v = ip__get_pixel(src, sx, sy, c);
                        if (v < minv) minv = v;
                    }
                }
                ip__set_pixel(dst, x, y, c, minv);
            }
        }
    }

    *out = dst;
    return IP_OK;
}

// ── Thresholding ───────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_threshold(ip_image** out, const ip_image* src,
                                          double thresh, double maxval,
                                          ip_thresh_type type) {
    if (!out || !src) return IP_ERR_NULL_ARG;
    if (src->channels != 1) return IP_ERR_INVALID;

    // For Otsu, compute optimal threshold from histogram (U8 only)
    if (type == IP_THRESH_OTSU) {
        if (src->dtype != IP_U8) return IP_ERR_DTYPE;

        // Build histogram
        int hist[256];
        memset(hist, 0, sizeof(hist));
        int total = src->width * src->height;
        for (int y = 0; y < src->height; y++) {
            for (int x = 0; x < src->width; x++) {
                uint8_t v = ip_get_u8(src, x, y, 0);
                hist[v]++;
            }
        }

        // Otsu's method
        double sum_total = 0;
        for (int i = 0; i < 256; i++) sum_total += i * (double)hist[i];

        double sum_bg = 0;
        int    w_bg = 0;
        double max_var = 0;
        int    best_t = 0;

        for (int t = 0; t < 256; t++) {
            w_bg += hist[t];
            if (w_bg == 0) continue;
            int w_fg = total - w_bg;
            if (w_fg == 0) break;

            sum_bg += t * (double)hist[t];
            double mean_bg = sum_bg / w_bg;
            double mean_fg = (sum_total - sum_bg) / w_fg;
            double diff = mean_bg - mean_fg;
            double var = (double)w_bg * w_fg * diff * diff;
            if (var > max_var) {
                max_var = var;
                best_t = t;
            }
        }
        thresh = best_t;
        // Fall through to binary thresholding with computed thresh
    }

    ip_image* dst = NULL;
    ip_status s = ip_create(src->width, src->height, 1, src->dtype, &dst);
    if (s != IP_OK) return s;

    bool invert = (type == IP_THRESH_BINARY_INV);

    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            double v = ip__get_pixel(src, x, y, 0);
            double result;
            if (invert) {
                result = (v > thresh) ? 0 : maxval;
            } else {
                result = (v > thresh) ? maxval : 0;
            }
            ip__set_pixel(dst, x, y, 0, result);
        }
    }

    *out = dst;
    return IP_OK;
}

// ── Connected Components (two-pass union-find, 8-connected) ────────────────

static int ip__uf_find(int* parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]]; // path compression
        x = parent[x];
    }
    return x;
}

static void ip__uf_union(int* parent, int* rank, int a, int b) {
    a = ip__uf_find(parent, a);
    b = ip__uf_find(parent, b);
    if (a == b) return;
    if (rank[a] < rank[b]) { int t = a; a = b; b = t; }
    parent[b] = a;
    if (rank[a] == rank[b]) rank[a]++;
}

IP_NODISCARD IPDEF ip_status ip_connected_components(const ip_image* binary,
                                                     ip_image** labels,
                                                     int* count) {
    if (!binary || !labels) return IP_ERR_NULL_ARG;
    if (binary->channels != 1) return IP_ERR_INVALID;

    int w = binary->width;
    int h = binary->height;
    int npx = w * h;

    // Label image (I32)
    ip_image* lbl = NULL;
    ip_status s = ip_create(w, h, 1, IP_I32, &lbl);
    if (s != IP_OK) return s;

    // Union-find arrays (max labels = npx)
    int* parent = (int*)IP_MALLOC(sizeof(int) * npx);
    int* uf_rank = (int*)IP_CALLOC(npx, sizeof(int));
    if (!parent || !uf_rank) {
        IP_FREE(parent);
        IP_FREE(uf_rank);
        ip_free(lbl);
        return IP_ERR_ALLOC;
    }

    int next_label = 1;

    // First pass: assign provisional labels
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double v = ip__get_pixel(binary, x, y, 0);
            if (v == 0) {
                ip_set_i32(lbl, x, y, 0, 0);
                continue;
            }

            // Check 8-connected neighbors that have already been visited
            // (above row and left pixel in current row)
            int neighbors[4];
            int nn = 0;

            // (x-1, y-1)
            if (x > 0 && y > 0) {
                int32_t nl = ip_get_i32(lbl, x-1, y-1, 0);
                if (nl > 0) neighbors[nn++] = nl;
            }
            // (x, y-1)
            if (y > 0) {
                int32_t nl = ip_get_i32(lbl, x, y-1, 0);
                if (nl > 0) neighbors[nn++] = nl;
            }
            // (x+1, y-1)
            if (x < w-1 && y > 0) {
                int32_t nl = ip_get_i32(lbl, x+1, y-1, 0);
                if (nl > 0) neighbors[nn++] = nl;
            }
            // (x-1, y)
            if (x > 0) {
                int32_t nl = ip_get_i32(lbl, x-1, y, 0);
                if (nl > 0) neighbors[nn++] = nl;
            }

            if (nn == 0) {
                // New label
                int l = next_label++;
                parent[l] = l;
                uf_rank[l] = 0;
                ip_set_i32(lbl, x, y, 0, l);
            } else {
                // Find minimum root
                int min_root = ip__uf_find(parent, neighbors[0]);
                for (int i = 1; i < nn; i++) {
                    int r = ip__uf_find(parent, neighbors[i]);
                    if (r < min_root) min_root = r;
                }
                ip_set_i32(lbl, x, y, 0, min_root);
                // Union all neighbors
                for (int i = 0; i < nn; i++) {
                    ip__uf_union(parent, uf_rank, min_root, neighbors[i]);
                }
            }
        }
    }

    // Second pass: resolve labels and compact
    // Build mapping from root labels to sequential IDs
    int* remap = (int*)IP_CALLOC(next_label, sizeof(int));
    if (!remap) {
        IP_FREE(parent); IP_FREE(uf_rank); ip_free(lbl);
        return IP_ERR_ALLOC;
    }

    int num_components = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int32_t l = ip_get_i32(lbl, x, y, 0);
            if (l == 0) continue;
            int root = ip__uf_find(parent, l);
            if (remap[root] == 0) {
                remap[root] = ++num_components;
            }
            ip_set_i32(lbl, x, y, 0, remap[root]);
        }
    }

    IP_FREE(parent);
    IP_FREE(uf_rank);
    IP_FREE(remap);

    *labels = lbl;
    if (count) *count = num_components;
    return IP_OK;
}

// ── Geometric Transforms ──────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_flip(ip_image** out, const ip_image* src,
                                     int flip_code) {
    if (!out || !src) return IP_ERR_NULL_ARG;

    ip_image* dst = NULL;
    ip_status s = ip_create(src->width, src->height, src->channels, src->dtype, &dst);
    if (s != IP_OK) return s;

    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            int sx = x, sy = y;
            if (flip_code == 0 || flip_code == -1) sy = src->height - 1 - y;
            if (flip_code == 1 || flip_code == -1) sx = src->width  - 1 - x;
            for (int c = 0; c < src->channels; c++) {
                ip__set_pixel(dst, x, y, c, ip__get_pixel(src, sx, sy, c));
            }
        }
    }

    *out = dst;
    return IP_OK;
}

IP_NODISCARD IPDEF ip_status ip_rotate90(ip_image** out, const ip_image* src,
                                         int times) {
    if (!out || !src) return IP_ERR_NULL_ARG;
    times = ((times % 4) + 4) % 4;
    if (times == 0) return ip_clone(out, src);

    int dw, dh;
    if (times == 2) {
        dw = src->width;
        dh = src->height;
    } else { // 1 or 3
        dw = src->height;
        dh = src->width;
    }

    ip_image* dst = NULL;
    ip_status s = ip_create(dw, dh, src->channels, src->dtype, &dst);
    if (s != IP_OK) return s;

    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            int dx, dy;
            switch (times) {
                case 1: dx = src->height - 1 - y; dy = x; break;
                case 2: dx = src->width - 1 - x;  dy = src->height - 1 - y; break;
                case 3: dx = y;                     dy = src->width - 1 - x; break;
                default: dx = x; dy = y; break;
            }
            for (int c = 0; c < src->channels; c++) {
                ip__set_pixel(dst, dx, dy, c, ip__get_pixel(src, x, y, c));
            }
        }
    }

    *out = dst;
    return IP_OK;
}

// ── Statistics ─────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_min_max(const ip_image* img,
                                        double* min_val, double* max_val) {
    if (!img) return IP_ERR_NULL_ARG;

    double mn = DBL_MAX, mx = -DBL_MAX;
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            for (int c = 0; c < img->channels; c++) {
                double v = ip__get_pixel(img, x, y, c);
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
        }
    }

    if (min_val) *min_val = mn;
    if (max_val) *max_val = mx;
    return IP_OK;
}

IP_NODISCARD IPDEF ip_status ip_normalize(ip_image** out, const ip_image* src,
                                          double alpha, double beta) {
    if (!out || !src) return IP_ERR_NULL_ARG;

    double mn, mx;
    ip_status s = ip_min_max(src, &mn, &mx);
    if (s != IP_OK) return s;

    ip_image* dst = NULL;
    s = ip_create(src->width, src->height, src->channels, src->dtype, &dst);
    if (s != IP_OK) return s;

    double range = mx - mn;
    if (range == 0) range = 1; // avoid division by zero

    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            for (int c = 0; c < src->channels; c++) {
                double v = ip__get_pixel(src, x, y, c);
                double nv = alpha + (v - mn) * (beta - alpha) / range;
                ip__set_pixel(dst, x, y, c, nv);
            }
        }
    }

    *out = dst;
    return IP_OK;
}

// ── Drawing ────────────────────────────────────────────────────────────────

IP_NODISCARD IPDEF ip_status ip_fill(ip_image* img, double value) {
    if (!img) return IP_ERR_NULL_ARG;

    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            for (int c = 0; c < img->channels; c++) {
                ip__set_pixel(img, x, y, c, value);
            }
        }
    }
    return IP_OK;
}

IP_NODISCARD IPDEF ip_status ip_draw_rect(ip_image* img, int x, int y,
                                          int w, int h, double value) {
    if (!img) return IP_ERR_NULL_ARG;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > img->width)  ? img->width  : x + w;
    int y1 = (y + h > img->height) ? img->height : y + h;

    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            for (int c = 0; c < img->channels; c++) {
                ip__set_pixel(img, px, py, c, value);
            }
        }
    }
    return IP_OK;
}

#endif // IP_IMPLEMENTATION
#endif // IMGPROC_H
