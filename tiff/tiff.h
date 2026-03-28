// tiff.h — Pure C23 single-channel TIFF library
// Single-header: declare API, then #define TF_IMPLEMENTATION in one .c file.
// Vesuvius Challenge / VC3D volumetric slice I/O.
#ifndef TIFF_H
#define TIFF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ── Version ─────────────────────────────────────────────────────────────────

#define TF_VERSION_MAJOR 0
#define TF_VERSION_MINOR 1
#define TF_VERSION_PATCH 0

// ── C23 Compat ──────────────────────────────────────────────────────────────

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define TF_NODISCARD    [[nodiscard]]
  #define TF_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define TF_NODISCARD    __attribute__((warn_unused_result))
  #define TF_MAYBE_UNUSED __attribute__((unused))
#else
  #define TF_NODISCARD
  #define TF_MAYBE_UNUSED
#endif

// ── Linkage ─────────────────────────────────────────────────────────────────

#ifndef TFDEF
  #ifdef TF_STATIC
    #define TFDEF static
  #else
    #define TFDEF extern
  #endif
#endif

// ── Allocator Hooks ─────────────────────────────────────────────────────────

#ifndef TF_MALLOC
  #include <stdlib.h>
  #define TF_MALLOC(sz)       malloc(sz)
  #define TF_REALLOC(p, sz)   realloc(p, sz)
  #define TF_FREE(p)          free(p)
  #define TF_CALLOC(n, sz)    calloc(n, sz)
#endif

// ── Status Codes ────────────────────────────────────────────────────────────

typedef enum tf_status {
    TF_OK = 0,
    TF_ERR_NULL_ARG,
    TF_ERR_ALLOC,
    TF_ERR_IO,
    TF_ERR_FORMAT,        // not a valid TIFF file
    TF_ERR_UNSUPPORTED,   // unsupported feature (big-endian, compression, etc.)
    TF_ERR_DTYPE,         // mismatched or invalid dtype
    TF_ERR_BOUNDS,        // pixel coordinates out of range
} tf_status;

// ── Data Types ──────────────────────────────────────────────────────────────

typedef enum tf_dtype {
    TF_U8 = 0,
    TF_U16,
    TF_F32,
} tf_dtype;

// ── Image ───────────────────────────────────────────────────────────────────

typedef struct tf_image tf_image;

// ── Utilities ───────────────────────────────────────────────────────────────

TFDEF const char* tf_status_str(tf_status s);
TFDEF const char* tf_version_str(void);
TFDEF const char* tf_dtype_str(tf_dtype d);
TFDEF size_t      tf_dtype_size(tf_dtype d);

// ── Image Lifecycle ─────────────────────────────────────────────────────────

TF_NODISCARD TFDEF tf_status tf_image_create(int width, int height,
                                             tf_dtype dtype, tf_image** out);
TF_NODISCARD TFDEF tf_status tf_image_from_data(void* data, int width,
                                                int height, tf_dtype dtype,
                                                tf_image** out);
TF_NODISCARD TFDEF tf_status tf_image_clone(tf_image** out,
                                            const tf_image* src);
TFDEF void tf_image_free(tf_image* img);

// ── Accessors ───────────────────────────────────────────────────────────────

TFDEF void*    tf_image_data(const tf_image* img);
TFDEF int      tf_image_width(const tf_image* img);
TFDEF int      tf_image_height(const tf_image* img);
TFDEF tf_dtype tf_image_dtype(const tf_image* img);
TFDEF size_t   tf_image_pixel_size(const tf_image* img);

// ── Pixel Access ────────────────────────────────────────────────────────────

TFDEF uint8_t  tf_get_u8(const tf_image* img, int x, int y);
TFDEF uint16_t tf_get_u16(const tf_image* img, int x, int y);
TFDEF float    tf_get_f32(const tf_image* img, int x, int y);

TFDEF void tf_set_u8(tf_image* img, int x, int y, uint8_t val);
TFDEF void tf_set_u16(tf_image* img, int x, int y, uint16_t val);
TFDEF void tf_set_f32(tf_image* img, int x, int y, float val);

// ── Reading ─────────────────────────────────────────────────────────────────

TF_NODISCARD TFDEF tf_status tf_read(const char* path, tf_image** out);
TF_NODISCARD TFDEF tf_status tf_read_header(const char* path, int* width,
                                            int* height, tf_dtype* dtype);

// ── Writing ─────────────────────────────────────────────────────────────────

TF_NODISCARD TFDEF tf_status tf_write(const char* path, const tf_image* img);

// ── Multi-Image (TIFF Stacks) ───────────────────────────────────────────────

TF_NODISCARD TFDEF tf_status tf_read_stack(const char* path, tf_image*** out,
                                           int* count);
TF_NODISCARD TFDEF tf_status tf_write_stack(const char* path,
                                            const tf_image** imgs, int count);

// ═══════════════════════════════════════════════════════════════════════════
// Implementation
// ═══════════════════════════════════════════════════════════════════════════

#ifdef TF_IMPLEMENTATION

// ── Internal Constants ──────────────────────────────────────────────────────

#define TF__TAG_IMAGE_WIDTH           256
#define TF__TAG_IMAGE_LENGTH          257
#define TF__TAG_BITS_PER_SAMPLE       258
#define TF__TAG_COMPRESSION           259
#define TF__TAG_PHOTOMETRIC           262
#define TF__TAG_STRIP_OFFSETS         273
#define TF__TAG_SAMPLES_PER_PIXEL     277
#define TF__TAG_ROWS_PER_STRIP        278
#define TF__TAG_STRIP_BYTE_COUNTS     279
#define TF__TAG_SAMPLE_FORMAT         339

// TIFF types
#define TF__TYPE_SHORT  3
#define TF__TYPE_LONG   4

#define TF__FLAG_OWNS_DATA (1u << 0)

// ── tf_image Definition ─────────────────────────────────────────────────────

struct tf_image {
    void*    data;
    int      width;
    int      height;
    tf_dtype dtype;
    uint32_t flags;
};

// ── Internal Helpers ────────────────────────────────────────────────────────

static uint16_t tf__r16(const uint8_t* p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t tf__r32(const uint8_t* p)
{
    return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static void tf__w16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void tf__w32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static size_t tf__pixel_size(tf_dtype d)
{
    switch (d) {
        case TF_U8:  return 1;
        case TF_U16: return 2;
        case TF_F32: return 4;
        default:     return 0;
    }
}

static tf_status tf__dtype_from_bps_fmt(int bps, int sample_fmt, tf_dtype* out)
{
    if (bps == 8  && (sample_fmt == 1 || sample_fmt == 0)) { *out = TF_U8;  return TF_OK; }
    if (bps == 16 && (sample_fmt == 1 || sample_fmt == 0)) { *out = TF_U16; return TF_OK; }
    if (bps == 32 && sample_fmt == 3)                      { *out = TF_F32; return TF_OK; }
    return TF_ERR_UNSUPPORTED;
}

static void tf__dtype_to_bps_fmt(tf_dtype d, int* bps, int* sample_fmt)
{
    switch (d) {
        case TF_U8:  *bps = 8;  *sample_fmt = 1; break;
        case TF_U16: *bps = 16; *sample_fmt = 1; break;
        case TF_F32: *bps = 32; *sample_fmt = 3; break;
    }
}

// ── Internal: IFD Parsing ───────────────────────────────────────────────────

typedef struct tf__ifd_info {
    int      width;
    int      height;
    int      bps;
    int      sample_fmt;
    int      compression;
    int      spp;
    int      rows_per_strip;
    uint32_t strip_offsets_val;     // value or offset to array
    uint32_t strip_byte_counts_val; // value or offset to array
    int      strip_offsets_count;
    int      strip_byte_counts_count;
    uint16_t strip_offsets_type;
    uint16_t strip_byte_counts_type;
    uint32_t next_ifd;
} tf__ifd_info;

static tf_status tf__parse_ifd(const uint8_t* buf, size_t buf_size,
                               uint32_t ifd_off, tf__ifd_info* info)
{
    memset(info, 0, sizeof(*info));
    info->compression = 1;
    info->spp = 1;
    info->sample_fmt = 1; // default UINT
    info->rows_per_strip = 0x7FFFFFFF; // default: all rows in one strip

    if (ifd_off + 2 > buf_size) return TF_ERR_FORMAT;
    uint16_t n_entries = tf__r16(&buf[ifd_off]);

    if (ifd_off + 2 + (size_t)n_entries * 12 + 4 > buf_size) return TF_ERR_FORMAT;

    for (int i = 0; i < n_entries; ++i) {
        const uint8_t* e = &buf[ifd_off + 2 + i * 12];
        uint16_t tag   = tf__r16(e);
        uint16_t type  = tf__r16(e + 2);
        uint32_t count = tf__r32(e + 4);

        // Get inline value (SHORT or LONG, count=1)
        uint32_t val;
        if (type == TF__TYPE_SHORT) val = tf__r16(e + 8);
        else                        val = tf__r32(e + 8);

        switch (tag) {
            case TF__TAG_IMAGE_WIDTH:       info->width = (int)val; break;
            case TF__TAG_IMAGE_LENGTH:      info->height = (int)val; break;
            case TF__TAG_BITS_PER_SAMPLE:   info->bps = (int)val; break;
            case TF__TAG_COMPRESSION:       info->compression = (int)val; break;
            case TF__TAG_PHOTOMETRIC:       break; // we accept any
            case TF__TAG_SAMPLES_PER_PIXEL: info->spp = (int)val; break;
            case TF__TAG_ROWS_PER_STRIP:    info->rows_per_strip = (int)val; break;
            case TF__TAG_SAMPLE_FORMAT:     info->sample_fmt = (int)val; break;
            case TF__TAG_STRIP_OFFSETS:
                info->strip_offsets_count = (int)count;
                info->strip_offsets_type  = type;
                info->strip_offsets_val   = (count == 1) ? val : tf__r32(e + 8);
                break;
            case TF__TAG_STRIP_BYTE_COUNTS:
                info->strip_byte_counts_count = (int)count;
                info->strip_byte_counts_type  = type;
                info->strip_byte_counts_val   = (count == 1) ? val : tf__r32(e + 8);
                break;
        }
    }

    info->next_ifd = tf__r32(&buf[ifd_off + 2 + n_entries * 12]);

    if (info->width <= 0 || info->height <= 0) return TF_ERR_FORMAT;
    if (info->compression != 1) return TF_ERR_UNSUPPORTED;
    if (info->spp != 1) return TF_ERR_UNSUPPORTED;

    return TF_OK;
}

static tf_status tf__read_image_data(const uint8_t* buf, size_t buf_size,
                                     const tf__ifd_info* info, void* dst,
                                     size_t total_bytes)
{
    size_t row_bytes = (size_t)info->width * (size_t)(info->bps / 8);

    if (info->strip_offsets_count <= 1) {
        // Single strip
        uint32_t off = info->strip_offsets_val;
        if ((size_t)off + total_bytes > buf_size) return TF_ERR_FORMAT;
        memcpy(dst, buf + off, total_bytes);
    } else {
        // Multiple strips
        uint32_t off_ptr = info->strip_offsets_val;
        uint32_t bc_ptr  = info->strip_byte_counts_val;
        size_t dst_pos = 0;
        int type_sz_off = (info->strip_offsets_type == TF__TYPE_SHORT) ? 2 : 4;
        int type_sz_bc  = (info->strip_byte_counts_type == TF__TYPE_SHORT) ? 2 : 4;

        for (int s = 0; s < info->strip_offsets_count && dst_pos < total_bytes; ++s) {
            uint32_t soff, sbc;
            if (type_sz_off == 2) soff = tf__r16(&buf[off_ptr + s * 2]);
            else                  soff = tf__r32(&buf[off_ptr + s * 4]);

            if (info->strip_byte_counts_count > 1) {
                if (type_sz_bc == 2) sbc = tf__r16(&buf[bc_ptr + s * 2]);
                else                 sbc = tf__r32(&buf[bc_ptr + s * 4]);
            } else {
                sbc = (uint32_t)row_bytes;
            }

            size_t to_copy = sbc;
            if (dst_pos + to_copy > total_bytes) to_copy = total_bytes - dst_pos;
            if ((size_t)soff + to_copy > buf_size) return TF_ERR_FORMAT;
            memcpy((uint8_t*)dst + dst_pos, buf + soff, to_copy);
            dst_pos += to_copy;
        }
    }

    return TF_OK;
}

// ── Internal: File I/O ──────────────────────────────────────────────────────

static tf_status tf__read_file(const char* path, uint8_t** out, size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    if (!f) return TF_ERR_IO;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return TF_ERR_IO; }
    fseek(f, 0, SEEK_SET);

    uint8_t* buf = (uint8_t*)TF_MALLOC((size_t)sz);
    if (!buf) { fclose(f); return TF_ERR_ALLOC; }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { TF_FREE(buf); return TF_ERR_IO; }

    *out = buf;
    *out_size = (size_t)sz;
    return TF_OK;
}

static tf_status tf__validate_header(const uint8_t* buf, size_t size,
                                     uint32_t* first_ifd)
{
    if (size < 8) return TF_ERR_FORMAT;
    if (buf[0] != 'I' || buf[1] != 'I') return TF_ERR_UNSUPPORTED; // only LE
    if (tf__r16(&buf[2]) != 42) return TF_ERR_FORMAT;
    *first_ifd = tf__r32(&buf[4]);
    if (*first_ifd == 0 || *first_ifd >= size) return TF_ERR_FORMAT;
    return TF_OK;
}

// ── Internal: Write Helpers ─────────────────────────────────────────────────

static void tf__write_tag(uint8_t* p, uint16_t tag, uint16_t type,
                          uint32_t count, uint32_t value)
{
    tf__w16(p,     tag);
    tf__w16(p + 2, type);
    tf__w32(p + 4, count);
    tf__w32(p + 8, value);
}

static tf_status tf__write_single_image(FILE* f, const tf_image* img,
                                        uint32_t* next_ifd_fixup_pos)
{
    int bps = 0, sample_fmt = 0;
    tf__dtype_to_bps_fmt(img->dtype, &bps, &sample_fmt);

    size_t row_bytes = (size_t)img->width * tf__pixel_size(img->dtype);
    size_t img_bytes = row_bytes * (size_t)img->height;
    int h = img->height;

    // Layout: IFD(2 + 10*12 + 4) + strip_offsets(h*4) + strip_byte_counts(h*4) + image_data
    int n_tags = 10;
    size_t ifd_size = 2 + (size_t)n_tags * 12 + 4;

    // Get current file position as start of this IFD
    long ifd_file_pos = ftell(f);
    if (ifd_file_pos < 0) return TF_ERR_IO;

    size_t after_ifd = (size_t)ifd_file_pos + ifd_size;
    size_t strips_off, strip_bytes_off, img_off;
    if (h > 1) {
        strips_off      = after_ifd;
        strip_bytes_off = strips_off + (size_t)h * 4;
        img_off         = strip_bytes_off + (size_t)h * 4;
    } else {
        strips_off      = 0;
        strip_bytes_off = 0;
        img_off         = after_ifd;
    }

    // Build IFD
    uint8_t ifd_buf[2 + 10 * 12 + 4];
    memset(ifd_buf, 0, sizeof(ifd_buf));
    tf__w16(ifd_buf, (uint16_t)n_tags);

    uint8_t* t = ifd_buf + 2;
    tf__write_tag(t + 0*12, TF__TAG_IMAGE_WIDTH,       TF__TYPE_SHORT, 1, (uint32_t)img->width);
    tf__write_tag(t + 1*12, TF__TAG_IMAGE_LENGTH,      TF__TYPE_SHORT, 1, (uint32_t)img->height);
    tf__write_tag(t + 2*12, TF__TAG_BITS_PER_SAMPLE,   TF__TYPE_SHORT, 1, (uint32_t)bps);
    tf__write_tag(t + 3*12, TF__TAG_COMPRESSION,       TF__TYPE_SHORT, 1, 1);
    tf__write_tag(t + 4*12, TF__TAG_PHOTOMETRIC,       TF__TYPE_SHORT, 1, 1); // MinIsBlack
    tf__write_tag(t + 5*12, TF__TAG_STRIP_OFFSETS,     TF__TYPE_LONG,  (uint32_t)h,
                  h == 1 ? (uint32_t)img_off : (uint32_t)strips_off);
    tf__write_tag(t + 6*12, TF__TAG_SAMPLES_PER_PIXEL, TF__TYPE_SHORT, 1, 1);
    tf__write_tag(t + 7*12, TF__TAG_ROWS_PER_STRIP,    TF__TYPE_SHORT, 1, 1);
    tf__write_tag(t + 8*12, TF__TAG_STRIP_BYTE_COUNTS, TF__TYPE_LONG,  (uint32_t)h,
                  h == 1 ? (uint32_t)row_bytes : (uint32_t)strip_bytes_off);
    tf__write_tag(t + 9*12, TF__TAG_SAMPLE_FORMAT,     TF__TYPE_SHORT, 1, (uint32_t)sample_fmt);

    // next_ifd = 0 for now; caller can fix up
    tf__w32(ifd_buf + 2 + n_tags * 12, 0);

    if (next_ifd_fixup_pos)
        *next_ifd_fixup_pos = (uint32_t)((size_t)ifd_file_pos + 2 + (size_t)n_tags * 12);

    if (fwrite(ifd_buf, 1, sizeof(ifd_buf), f) != sizeof(ifd_buf)) return TF_ERR_IO;

    // Strip offsets and byte counts (if h > 1)
    if (h > 1) {
        for (int r = 0; r < h; ++r) {
            uint8_t tmp[4];
            tf__w32(tmp, (uint32_t)(img_off + (size_t)r * row_bytes));
            if (fwrite(tmp, 1, 4, f) != 4) return TF_ERR_IO;
        }
        for (int r = 0; r < h; ++r) {
            uint8_t tmp[4];
            tf__w32(tmp, (uint32_t)row_bytes);
            if (fwrite(tmp, 1, 4, f) != 4) return TF_ERR_IO;
        }
    }

    // Image data
    if (fwrite(img->data, 1, img_bytes, f) != img_bytes) return TF_ERR_IO;

    return TF_OK;
}

// ── Utilities Implementation ────────────────────────────────────────────────

TFDEF const char* tf_status_str(tf_status s)
{
    switch (s) {
        case TF_OK:              return "ok";
        case TF_ERR_NULL_ARG:    return "null argument";
        case TF_ERR_ALLOC:       return "allocation failed";
        case TF_ERR_IO:          return "I/O error";
        case TF_ERR_FORMAT:      return "invalid TIFF format";
        case TF_ERR_UNSUPPORTED: return "unsupported feature";
        case TF_ERR_DTYPE:       return "dtype error";
        case TF_ERR_BOUNDS:      return "out of bounds";
        default:                 return "unknown error";
    }
}

TFDEF const char* tf_version_str(void)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d",
             TF_VERSION_MAJOR, TF_VERSION_MINOR, TF_VERSION_PATCH);
    return buf;
}

TFDEF const char* tf_dtype_str(tf_dtype d)
{
    switch (d) {
        case TF_U8:  return "u8";
        case TF_U16: return "u16";
        case TF_F32: return "f32";
        default:     return "unknown";
    }
}

TFDEF size_t tf_dtype_size(tf_dtype d)
{
    return tf__pixel_size(d);
}

// ── Image Lifecycle Implementation ──────────────────────────────────────────

TFDEF tf_status tf_image_create(int width, int height, tf_dtype dtype,
                                tf_image** out)
{
    if (!out) return TF_ERR_NULL_ARG;
    if (width <= 0 || height <= 0) return TF_ERR_BOUNDS;

    tf_image* img = (tf_image*)TF_MALLOC(sizeof(tf_image));
    if (!img) return TF_ERR_ALLOC;

    size_t ps = tf__pixel_size(dtype);
    size_t total = (size_t)width * (size_t)height * ps;

    img->data = TF_CALLOC(1, total);
    if (!img->data) { TF_FREE(img); return TF_ERR_ALLOC; }

    img->width  = width;
    img->height = height;
    img->dtype  = dtype;
    img->flags  = TF__FLAG_OWNS_DATA;

    *out = img;
    return TF_OK;
}

TFDEF tf_status tf_image_from_data(void* data, int width, int height,
                                   tf_dtype dtype, tf_image** out)
{
    if (!out || !data) return TF_ERR_NULL_ARG;
    if (width <= 0 || height <= 0) return TF_ERR_BOUNDS;

    tf_image* img = (tf_image*)TF_MALLOC(sizeof(tf_image));
    if (!img) return TF_ERR_ALLOC;

    img->data   = data;
    img->width  = width;
    img->height = height;
    img->dtype  = dtype;
    img->flags  = 0; // does NOT own data

    *out = img;
    return TF_OK;
}

TFDEF tf_status tf_image_clone(tf_image** out, const tf_image* src)
{
    if (!out || !src) return TF_ERR_NULL_ARG;

    tf_status s = tf_image_create(src->width, src->height, src->dtype, out);
    if (s != TF_OK) return s;

    size_t total = (size_t)src->width * (size_t)src->height * tf__pixel_size(src->dtype);
    memcpy((*out)->data, src->data, total);
    return TF_OK;
}

TFDEF void tf_image_free(tf_image* img)
{
    if (!img) return;
    if (img->flags & TF__FLAG_OWNS_DATA) TF_FREE(img->data);
    TF_FREE(img);
}

// ── Accessors Implementation ────────────────────────────────────────────────

TFDEF void* tf_image_data(const tf_image* img)
{
    return img ? img->data : NULL;
}

TFDEF int tf_image_width(const tf_image* img)
{
    return img ? img->width : 0;
}

TFDEF int tf_image_height(const tf_image* img)
{
    return img ? img->height : 0;
}

TFDEF tf_dtype tf_image_dtype(const tf_image* img)
{
    return img ? img->dtype : TF_U8;
}

TFDEF size_t tf_image_pixel_size(const tf_image* img)
{
    return img ? tf__pixel_size(img->dtype) : 0;
}

// ── Pixel Access Implementation ─────────────────────────────────────────────

TFDEF uint8_t tf_get_u8(const tf_image* img, int x, int y)
{
    if (!img || x < 0 || x >= img->width || y < 0 || y >= img->height) return 0;
    return ((const uint8_t*)img->data)[y * img->width + x];
}

TFDEF uint16_t tf_get_u16(const tf_image* img, int x, int y)
{
    if (!img || x < 0 || x >= img->width || y < 0 || y >= img->height) return 0;
    return ((const uint16_t*)img->data)[y * img->width + x];
}

TFDEF float tf_get_f32(const tf_image* img, int x, int y)
{
    if (!img || x < 0 || x >= img->width || y < 0 || y >= img->height) return 0.0f;
    return ((const float*)img->data)[y * img->width + x];
}

TFDEF void tf_set_u8(tf_image* img, int x, int y, uint8_t val)
{
    if (!img || x < 0 || x >= img->width || y < 0 || y >= img->height) return;
    ((uint8_t*)img->data)[y * img->width + x] = val;
}

TFDEF void tf_set_u16(tf_image* img, int x, int y, uint16_t val)
{
    if (!img || x < 0 || x >= img->width || y < 0 || y >= img->height) return;
    ((uint16_t*)img->data)[y * img->width + x] = val;
}

TFDEF void tf_set_f32(tf_image* img, int x, int y, float val)
{
    if (!img || x < 0 || x >= img->width || y < 0 || y >= img->height) return;
    ((float*)img->data)[y * img->width + x] = val;
}

// ── Reading Implementation ──────────────────────────────────────────────────

TFDEF tf_status tf_read(const char* path, tf_image** out)
{
    if (!path || !out) return TF_ERR_NULL_ARG;

    uint8_t* buf = NULL;
    size_t buf_size = 0;
    tf_status s = tf__read_file(path, &buf, &buf_size);
    if (s != TF_OK) return s;

    uint32_t ifd_off = 0;
    s = tf__validate_header(buf, buf_size, &ifd_off);
    if (s != TF_OK) { TF_FREE(buf); return s; }

    tf__ifd_info info;
    s = tf__parse_ifd(buf, buf_size, ifd_off, &info);
    if (s != TF_OK) { TF_FREE(buf); return s; }

    tf_dtype dtype;
    s = tf__dtype_from_bps_fmt(info.bps, info.sample_fmt, &dtype);
    if (s != TF_OK) { TF_FREE(buf); return s; }

    tf_image* img = NULL;
    s = tf_image_create(info.width, info.height, dtype, &img);
    if (s != TF_OK) { TF_FREE(buf); return s; }

    size_t total = (size_t)info.width * (size_t)info.height * tf__pixel_size(dtype);
    s = tf__read_image_data(buf, buf_size, &info, img->data, total);
    TF_FREE(buf);

    if (s != TF_OK) { tf_image_free(img); return s; }

    *out = img;
    return TF_OK;
}

TFDEF tf_status tf_read_header(const char* path, int* width, int* height,
                               tf_dtype* dtype)
{
    if (!path) return TF_ERR_NULL_ARG;

    uint8_t* buf = NULL;
    size_t buf_size = 0;
    tf_status s = tf__read_file(path, &buf, &buf_size);
    if (s != TF_OK) return s;

    uint32_t ifd_off = 0;
    s = tf__validate_header(buf, buf_size, &ifd_off);
    if (s != TF_OK) { TF_FREE(buf); return s; }

    tf__ifd_info info;
    s = tf__parse_ifd(buf, buf_size, ifd_off, &info);
    if (s != TF_OK) { TF_FREE(buf); return s; }

    tf_dtype dt;
    s = tf__dtype_from_bps_fmt(info.bps, info.sample_fmt, &dt);
    TF_FREE(buf);
    if (s != TF_OK) return s;

    if (width)  *width  = info.width;
    if (height) *height = info.height;
    if (dtype)  *dtype  = dt;

    return TF_OK;
}

// ── Writing Implementation ──────────────────────────────────────────────────

TFDEF tf_status tf_write(const char* path, const tf_image* img)
{
    if (!path || !img) return TF_ERR_NULL_ARG;

    FILE* f = fopen(path, "wb");
    if (!f) return TF_ERR_IO;

    // Write TIFF header: "II" + 42 + offset to first IFD (8)
    uint8_t header[8];
    header[0] = 'I'; header[1] = 'I';
    tf__w16(header + 2, 42);
    tf__w32(header + 4, 8); // IFD starts right after header

    if (fwrite(header, 1, 8, f) != 8) { fclose(f); return TF_ERR_IO; }

    tf_status s = tf__write_single_image(f, img, NULL);
    fclose(f);
    return s;
}

// ── Multi-Image Implementation ──────────────────────────────────────────────

TFDEF tf_status tf_read_stack(const char* path, tf_image*** out, int* count)
{
    if (!path || !out || !count) return TF_ERR_NULL_ARG;

    uint8_t* buf = NULL;
    size_t buf_size = 0;
    tf_status s = tf__read_file(path, &buf, &buf_size);
    if (s != TF_OK) return s;

    uint32_t ifd_off = 0;
    s = tf__validate_header(buf, buf_size, &ifd_off);
    if (s != TF_OK) { TF_FREE(buf); return s; }

    // First pass: count IFDs
    int n = 0;
    uint32_t cur = ifd_off;
    while (cur != 0 && cur < buf_size) {
        if (cur + 2 > buf_size) break;
        uint16_t n_entries = tf__r16(&buf[cur]);
        if (cur + 2 + (size_t)n_entries * 12 + 4 > buf_size) break;
        n++;
        cur = tf__r32(&buf[cur + 2 + n_entries * 12]);
    }

    if (n == 0) { TF_FREE(buf); return TF_ERR_FORMAT; }

    tf_image** imgs = (tf_image**)TF_CALLOC((size_t)n, sizeof(tf_image*));
    if (!imgs) { TF_FREE(buf); return TF_ERR_ALLOC; }

    // Second pass: read each IFD
    cur = ifd_off;
    for (int i = 0; i < n; ++i) {
        tf__ifd_info info;
        s = tf__parse_ifd(buf, buf_size, cur, &info);
        if (s != TF_OK) goto fail;

        tf_dtype dtype;
        s = tf__dtype_from_bps_fmt(info.bps, info.sample_fmt, &dtype);
        if (s != TF_OK) goto fail;

        s = tf_image_create(info.width, info.height, dtype, &imgs[i]);
        if (s != TF_OK) goto fail;

        size_t total = (size_t)info.width * (size_t)info.height * tf__pixel_size(dtype);
        s = tf__read_image_data(buf, buf_size, &info, imgs[i]->data, total);
        if (s != TF_OK) goto fail;

        cur = info.next_ifd;
    }

    TF_FREE(buf);
    *out = imgs;
    *count = n;
    return TF_OK;

fail:
    for (int j = 0; j < n; ++j) tf_image_free(imgs[j]);
    TF_FREE(imgs);
    TF_FREE(buf);
    return s;
}

TFDEF tf_status tf_write_stack(const char* path, const tf_image** imgs,
                               int count)
{
    if (!path || !imgs || count <= 0) return TF_ERR_NULL_ARG;
    for (int i = 0; i < count; ++i)
        if (!imgs[i]) return TF_ERR_NULL_ARG;

    FILE* f = fopen(path, "wb");
    if (!f) return TF_ERR_IO;

    // Header: "II" + 42 + offset to first IFD (8)
    uint8_t header[8];
    header[0] = 'I'; header[1] = 'I';
    tf__w16(header + 2, 42);
    tf__w32(header + 4, 8);
    if (fwrite(header, 1, 8, f) != 8) { fclose(f); return TF_ERR_IO; }

    uint32_t prev_next_ifd_pos = 0;
    tf_status s = TF_OK;

    for (int i = 0; i < count; ++i) {
        // If not the first image, fix up previous IFD's next_ifd pointer
        if (i > 0 && prev_next_ifd_pos > 0) {
            long cur_pos = ftell(f);
            if (cur_pos < 0) { fclose(f); return TF_ERR_IO; }

            fseek(f, (long)prev_next_ifd_pos, SEEK_SET);
            uint8_t tmp[4];
            tf__w32(tmp, (uint32_t)cur_pos);
            if (fwrite(tmp, 1, 4, f) != 4) { fclose(f); return TF_ERR_IO; }
            fseek(f, cur_pos, SEEK_SET);
        }

        uint32_t fixup_pos = 0;
        s = tf__write_single_image(f, imgs[i], &fixup_pos);
        if (s != TF_OK) { fclose(f); return s; }

        prev_next_ifd_pos = fixup_pos;
    }

    fclose(f);
    return TF_OK;
}

#endif // TF_IMPLEMENTATION
#endif // TIFF_H
