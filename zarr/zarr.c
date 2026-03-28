// zarr.c — Pure C23 zarr library for volumetric X-ray data
// Built on c-blosc2 + vl264. Read any zarr, write one canonical format.
#include "zarr.h"
#include <blosc2.h>
#include <b2nd.h>
#include <vl264.h>

#include <zstd.h>
#include <zlib.h>
#include <lz4.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <float.h>

// ── Internal Constants ──────────────────────────────────────────────────────

#define ZARR_VL264_COMPCODE     244   // blosc2 user codec range [160..255]
#define ZARR_VL264_COMPNAME     "vl264"
#define ZARR_VL264_VERSION      1
#define ZARR_JSON_MAX           (64 * 1024)  // max metadata JSON size
#define ZARR_PATH_MAX           4096

// ── Array struct ────────────────────────────────────────────────────────────

struct zarr_array {
    b2nd_array_t*  b2;          // blosc2 ndim array (owns the data)
    int8_t         ndim;
    int64_t        shape[ZARR_MAX_DIM];
    char           path[ZARR_PATH_MAX];
};

// ── Static State ────────────────────────────────────────────────────────────

static bool         g_initialized = false;
static vl264_enc*   g_encoder = nullptr;
static vl264_dec*   g_decoder = nullptr;

// ── vl264 ↔ blosc2 Codec Bridge ────────────────────────────────────────────

static int zarr__vl264_encode(const uint8_t* input, int32_t input_len,
                              uint8_t* output, int32_t output_len,
                              uint8_t meta, blosc2_cparams* cparams,
                              const void* chunk) {
    (void)meta; (void)cparams; (void)chunk;

    if (input_len != ZARR_CHUNK_VOXELS) return BLOSC2_ERROR_CODEC_PARAM;
    if (!g_encoder) return BLOSC2_ERROR_CODEC_PARAM;

    // Let vl264 allocate its own buffer (data=NULL), then copy into blosc2's output
    vl264_buf buf = { .data = nullptr, .size = 0, .capacity = 0 };
    vl264_status s = vl264_encode(g_encoder, input, nullptr, nullptr, &buf);
    if (s != VL264_OK) return BLOSC2_ERROR_CODEC_PARAM;
    if ((int32_t)buf.size > output_len) {
        vl264_free(buf.data);
        return BLOSC2_ERROR_WRITE_BUFFER;
    }
    memcpy(output, buf.data, buf.size);
    int result = (int)buf.size;
    vl264_free(buf.data);
    return result;
}

static int zarr__vl264_decode(const uint8_t* input, int32_t input_len,
                              uint8_t* output, int32_t output_len,
                              uint8_t meta, blosc2_dparams* dparams,
                              const void* chunk) {
    (void)meta; (void)dparams; (void)chunk;

    if (output_len != ZARR_CHUNK_VOXELS) return BLOSC2_ERROR_CODEC_PARAM;
    if (!g_decoder) return BLOSC2_ERROR_CODEC_PARAM;

    vl264_status s = vl264_decode(g_decoder, input, (size_t)input_len,
                                  nullptr, nullptr, output);
    if (s != VL264_OK) return BLOSC2_ERROR_CODEC_PARAM;
    return output_len;
}

// ── Init / Destroy ──────────────────────────────────────────────────────────

zarr_status zarr_init(void) {
    if (g_initialized) return ZARR_OK;

    blosc2_init();

    vl264_cfg cfg = vl264_default_cfg();
    g_encoder = vl264_enc_create(&cfg);
    if (!g_encoder) return ZARR_ERR_VL264;

    g_decoder = vl264_dec_create();
    if (!g_decoder) {
        vl264_enc_destroy(g_encoder);
        g_encoder = nullptr;
        return ZARR_ERR_VL264;
    }

    blosc2_codec vl264_codec = {
        .compcode = ZARR_VL264_COMPCODE,
        .compname = ZARR_VL264_COMPNAME,
        .complib  = ZARR_VL264_VERSION,
        .version  = ZARR_VL264_VERSION,
        .encoder  = zarr__vl264_encode,
        .decoder  = zarr__vl264_decode,
    };
    int rc = blosc2_register_codec(&vl264_codec);
    if (rc < 0) {
        vl264_enc_destroy(g_encoder);
        vl264_dec_destroy(g_decoder);
        g_encoder = nullptr;
        g_decoder = nullptr;
        return ZARR_ERR_BLOSC;
    }

    g_initialized = true;
    return ZARR_OK;
}

void zarr_destroy(void) {
    if (!g_initialized) return;
    if (g_encoder) { vl264_enc_destroy(g_encoder); g_encoder = nullptr; }
    if (g_decoder) { vl264_dec_destroy(g_decoder); g_decoder = nullptr; }
    blosc2_destroy();
    g_initialized = false;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

static int64_t zarr__region_size(int8_t ndim, const int64_t* start, const int64_t* stop) {
    int64_t n = 1;
    for (int8_t i = 0; i < ndim; i++) {
        n *= (stop[i] - start[i]);
    }
    return n;
}

static bool zarr__file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool zarr__is_dir(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Read a binary file with configurable max size (0 = no limit)
static uint8_t* zarr__read_file_raw(const char* path, size_t* out_len, size_t max_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || (max_size > 0 && (size_t)len > max_size)) {
        fclose(f);
        return nullptr;
    }

    uint8_t* buf = ZARR_MALLOC((size_t)len + 1);
    if (!buf) { fclose(f); return nullptr; }

    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) { ZARR_FREE(buf); return nullptr; }

    buf[len] = '\0';
    if (out_len) *out_len = (size_t)len;
    return buf;
}

// Read a file with a size limit (for JSON metadata)
static char* zarr__read_file(const char* path, size_t* out_len) {
    return (char*)zarr__read_file_raw(path, out_len, ZARR_JSON_MAX);
}

static bool zarr__write_file(const char* path, const char* data, size_t len) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t wr = fwrite(data, 1, len, f);
    fclose(f);
    return wr == len;
}

static bool zarr__mkdir_p(const char* path) {
    char tmp[ZARR_PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

// ── Create ──────────────────────────────────────────────────────────────────

zarr_status zarr_create(zarr_array** out, const char* path,
                        int8_t ndim, const int64_t* shape) {
    if (!out || !path || !shape) return ZARR_ERR_NULL_ARG;
    if (!g_initialized) return ZARR_ERR_BLOSC;
    if (ndim < 1 || ndim > ZARR_MAX_DIM) return ZARR_ERR_BOUNDS;

    for (int8_t i = 0; i < ndim; i++) {
        if (shape[i] <= 0) return ZARR_ERR_BOUNDS;
    }

    zarr_array* arr = ZARR_CALLOC(1, sizeof(zarr_array));
    if (!arr) return ZARR_ERR_ALLOC;

    arr->ndim = ndim;
    memcpy(arr->shape, shape, ndim * sizeof(int64_t));
    snprintf(arr->path, ZARR_PATH_MAX, "%s", path);

    // Configure blosc2 compression: vl264, no shuffle, no filters
    blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
    cparams.compcode = ZARR_VL264_COMPCODE;
    cparams.clevel = 5;
    cparams.typesize = 1;  // uint8
    cparams.blocksize = ZARR_CHUNK_VOXELS;
    cparams.nthreads = 1;
    cparams.splitmode = BLOSC_NEVER_SPLIT;
    // Disable all filters — vl264 handles everything
    memset(cparams.filters, 0, sizeof(cparams.filters));
    memset(cparams.filters_meta, 0, sizeof(cparams.filters_meta));

    blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;
    dparams.nthreads = 1;

    // Ensure parent directory of the path exists (blosc2 creates the final dir)
    {
        char parent[ZARR_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", path);
        char* last_slash = strrchr(parent, '/');
        if (last_slash && last_slash != parent) {
            *last_slash = '\0';
            zarr__mkdir_p(parent);
        }
    }

    blosc2_storage storage = {
        .contiguous = false,  // sparse frame (directory of chunks)
        .urlpath = (char*)path,
        .cparams = &cparams,
        .dparams = &dparams,
        .io = nullptr,
    };

    // chunk == block == 128³ for vl264
    int32_t chunkshape[ZARR_MAX_DIM];
    int32_t blockshape[ZARR_MAX_DIM];
    for (int8_t i = 0; i < ndim; i++) {
        chunkshape[i] = ZARR_CHUNK_DIM;
        blockshape[i] = ZARR_CHUNK_DIM;
    }

    b2nd_context_t* ctx = b2nd_create_ctx(&storage, ndim, shape, chunkshape,
                                          blockshape, "|u1", DTYPE_NUMPY_FORMAT,
                                          nullptr, 0);
    if (!ctx) {
        ZARR_FREE(arr);
        return ZARR_ERR_BLOSC;
    }

    int rc = b2nd_empty(ctx, &arr->b2);
    b2nd_free_ctx(ctx);
    if (rc < 0) {
        ZARR_FREE(arr);
        return ZARR_ERR_BLOSC;
    }

    *out = arr;
    return ZARR_OK;
}

// ── Open ────────────────────────────────────────────────────────────────────

zarr_status zarr_open(zarr_array** out, const char* path) {
    if (!out || !path) return ZARR_ERR_NULL_ARG;
    if (!g_initialized) return ZARR_ERR_BLOSC;

    zarr_array* arr = ZARR_CALLOC(1, sizeof(zarr_array));
    if (!arr) return ZARR_ERR_ALLOC;

    snprintf(arr->path, ZARR_PATH_MAX, "%s", path);

    int rc = b2nd_open(path, &arr->b2);
    if (rc < 0) {
        ZARR_FREE(arr);
        return ZARR_ERR_IO;
    }

    arr->ndim = arr->b2->ndim;
    memcpy(arr->shape, arr->b2->shape, arr->ndim * sizeof(int64_t));

    *out = arr;
    return ZARR_OK;
}

// ── Close ───────────────────────────────────────────────────────────────────

void zarr_close(zarr_array* arr) {
    if (!arr) return;
    if (arr->b2) b2nd_free(arr->b2);
    ZARR_FREE(arr);
}

// ── Data Access ─────────────────────────────────────────────────────────────

zarr_status zarr_read(const zarr_array* arr, const int64_t* start,
                      const int64_t* stop, uint8_t* buf, int64_t bufsize) {
    if (!arr || !start || !stop || !buf) return ZARR_ERR_NULL_ARG;

    int64_t n = zarr__region_size(arr->ndim, start, stop);
    if (n <= 0 || bufsize < n) return ZARR_ERR_BOUNDS;

    int64_t buffershape[ZARR_MAX_DIM];
    for (int8_t i = 0; i < arr->ndim; i++) {
        buffershape[i] = stop[i] - start[i];
    }

    int rc = b2nd_get_slice_cbuffer(arr->b2, start, stop, buf, buffershape, bufsize);
    return rc < 0 ? ZARR_ERR_BLOSC : ZARR_OK;
}

zarr_status zarr_write(zarr_array* arr, const int64_t* start,
                       const int64_t* stop, const uint8_t* buf, int64_t bufsize) {
    if (!arr || !start || !stop || !buf) return ZARR_ERR_NULL_ARG;

    int64_t n = zarr__region_size(arr->ndim, start, stop);
    if (n <= 0 || bufsize < n) return ZARR_ERR_BOUNDS;

    int64_t buffershape[ZARR_MAX_DIM];
    for (int8_t i = 0; i < arr->ndim; i++) {
        buffershape[i] = stop[i] - start[i];
    }

    int rc = b2nd_set_slice_cbuffer(buf, buffershape, bufsize, start, stop, arr->b2);
    return rc < 0 ? ZARR_ERR_BLOSC : ZARR_OK;
}

zarr_status zarr_read_chunk(const zarr_array* arr, const int64_t* chunk_coords,
                            uint8_t* buf) {
    if (!arr || !chunk_coords || !buf) return ZARR_ERR_NULL_ARG;

    int64_t start[ZARR_MAX_DIM], stop[ZARR_MAX_DIM];
    int64_t buffershape[ZARR_MAX_DIM];
    for (int8_t i = 0; i < arr->ndim; i++) {
        start[i] = chunk_coords[i] * ZARR_CHUNK_DIM;
        stop[i] = start[i] + ZARR_CHUNK_DIM;
        if (stop[i] > arr->shape[i]) stop[i] = arr->shape[i];
        if (start[i] >= arr->shape[i]) return ZARR_ERR_BOUNDS;
        buffershape[i] = stop[i] - start[i];
    }

    int rc = b2nd_get_slice_cbuffer(arr->b2, start, stop, buf, buffershape,
                                    ZARR_CHUNK_VOXELS);
    return rc < 0 ? ZARR_ERR_BLOSC : ZARR_OK;
}

zarr_status zarr_write_chunk(zarr_array* arr, const int64_t* chunk_coords,
                             const uint8_t* buf) {
    if (!arr || !chunk_coords || !buf) return ZARR_ERR_NULL_ARG;

    int64_t start[ZARR_MAX_DIM], stop[ZARR_MAX_DIM];
    int64_t buffershape[ZARR_MAX_DIM];
    for (int8_t i = 0; i < arr->ndim; i++) {
        start[i] = chunk_coords[i] * ZARR_CHUNK_DIM;
        stop[i] = start[i] + ZARR_CHUNK_DIM;
        if (stop[i] > arr->shape[i]) stop[i] = arr->shape[i];
        if (start[i] >= arr->shape[i]) return ZARR_ERR_BOUNDS;
        buffershape[i] = stop[i] - start[i];
    }

    int rc = b2nd_set_slice_cbuffer(buf, buffershape, ZARR_CHUNK_VOXELS,
                                    start, stop, arr->b2);
    return rc < 0 ? ZARR_ERR_BLOSC : ZARR_OK;
}

// ── Metadata ────────────────────────────────────────────────────────────────

int8_t zarr_ndim(const zarr_array* arr) {
    return arr ? arr->ndim : 0;
}

const int64_t* zarr_shape(const zarr_array* arr) {
    return arr ? arr->shape : nullptr;
}

int64_t zarr_nchunks(const zarr_array* arr) {
    if (!arr) return 0;
    int64_t n = 1;
    for (int8_t i = 0; i < arr->ndim; i++) {
        n *= (arr->shape[i] + ZARR_CHUNK_DIM - 1) / ZARR_CHUNK_DIM;
    }
    return n;
}

// ── Minimal JSON Parser ────────────────────────────────────────────────────
// Just enough to extract zarr v2/v3 metadata fields from .zarray / zarr.json.

typedef struct {
    const char* data;
    size_t      pos;
    size_t      len;
} zarr__json;

static void zarr__json_skip_ws(zarr__json* j) {
    while (j->pos < j->len) {
        char c = j->data[j->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        j->pos++;
    }
}

static bool zarr__json_match(zarr__json* j, char c) {
    zarr__json_skip_ws(j);
    if (j->pos < j->len && j->data[j->pos] == c) {
        j->pos++;
        return true;
    }
    return false;
}

// Extract a JSON string value (no escape handling beyond \")
static bool zarr__json_string(zarr__json* j, char* out, size_t maxlen) {
    zarr__json_skip_ws(j);
    if (j->pos >= j->len || j->data[j->pos] != '"') return false;
    j->pos++;
    size_t start = j->pos;
    while (j->pos < j->len && j->data[j->pos] != '"') {
        if (j->data[j->pos] == '\\') j->pos++; // skip escaped char
        j->pos++;
    }
    if (j->pos >= j->len) return false;
    size_t slen = j->pos - start;
    if (slen >= maxlen) slen = maxlen - 1;
    memcpy(out, j->data + start, slen);
    out[slen] = '\0';
    j->pos++; // skip closing quote
    return true;
}

static bool zarr__json_int(zarr__json* j, int64_t* out) {
    zarr__json_skip_ws(j);
    char* end;
    *out = strtoll(j->data + j->pos, &end, 10);
    if (end == j->data + j->pos) return false;
    j->pos = (size_t)(end - j->data);
    return true;
}

ZARR_MAYBE_UNUSED
static bool zarr__json_float(zarr__json* j, double* out) {
    zarr__json_skip_ws(j);
    char* end;
    *out = strtod(j->data + j->pos, &end);
    if (end == j->data + j->pos) return false;
    j->pos = (size_t)(end - j->data);
    return true;
}

// Parse a JSON array of integers: [1, 2, 3]
static bool zarr__json_int_array(zarr__json* j, int64_t* out, int* count, int maxcount) {
    if (!zarr__json_match(j, '[')) return false;
    *count = 0;
    zarr__json_skip_ws(j);
    if (j->pos < j->len && j->data[j->pos] == ']') { j->pos++; return true; }
    while (*count < maxcount) {
        int64_t val;
        if (!zarr__json_int(j, &val)) return false;
        out[(*count)++] = val;
        zarr__json_skip_ws(j);
        if (j->pos < j->len && j->data[j->pos] == ',') { j->pos++; continue; }
        break;
    }
    return zarr__json_match(j, ']');
}

// Skip a JSON value (string, number, object, array, true, false, null)
static bool zarr__json_skip_value(zarr__json* j) {
    zarr__json_skip_ws(j);
    if (j->pos >= j->len) return false;
    char c = j->data[j->pos];
    if (c == '"') {
        j->pos++;
        while (j->pos < j->len && j->data[j->pos] != '"') {
            if (j->data[j->pos] == '\\') j->pos++;
            j->pos++;
        }
        if (j->pos < j->len) j->pos++;
        return true;
    }
    if (c == '{') {
        int depth = 1;
        j->pos++;
        while (j->pos < j->len && depth > 0) {
            if (j->data[j->pos] == '{') depth++;
            else if (j->data[j->pos] == '}') depth--;
            else if (j->data[j->pos] == '"') {
                j->pos++;
                while (j->pos < j->len && j->data[j->pos] != '"') {
                    if (j->data[j->pos] == '\\') j->pos++;
                    j->pos++;
                }
            }
            j->pos++;
        }
        return true;
    }
    if (c == '[') {
        int depth = 1;
        j->pos++;
        while (j->pos < j->len && depth > 0) {
            if (j->data[j->pos] == '[') depth++;
            else if (j->data[j->pos] == ']') depth--;
            else if (j->data[j->pos] == '"') {
                j->pos++;
                while (j->pos < j->len && j->data[j->pos] != '"') {
                    if (j->data[j->pos] == '\\') j->pos++;
                    j->pos++;
                }
            }
            j->pos++;
        }
        return true;
    }
    // number, true, false, null — skip until delimiter
    while (j->pos < j->len) {
        c = j->data[j->pos];
        if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
        j->pos++;
    }
    return true;
}

// Find a key in the current JSON object level and position after the ':'
static bool zarr__json_find_key(zarr__json* j, const char* key) {
    // Reset to start and skip opening brace
    j->pos = 0;
    if (!zarr__json_match(j, '{')) return false;
    while (j->pos < j->len) {
        zarr__json_skip_ws(j);
        if (j->pos < j->len && j->data[j->pos] == '}') return false;
        char name[256];
        if (!zarr__json_string(j, name, sizeof(name))) return false;
        if (!zarr__json_match(j, ':')) return false;
        if (strcmp(name, key) == 0) return true;
        zarr__json_skip_value(j);
        zarr__json_skip_ws(j);
        if (j->pos < j->len && j->data[j->pos] == ',') j->pos++;
    }
    return false;
}

// ── Zarr Metadata Structures ────────────────────────────────────────────────

typedef enum {
    ZARR_FMT_V2 = 2,
    ZARR_FMT_V3 = 3,
} zarr__format;

typedef enum {
    ZARR_DT_BOOL,
    ZARR_DT_INT8, ZARR_DT_INT16, ZARR_DT_INT32, ZARR_DT_INT64,
    ZARR_DT_UINT8, ZARR_DT_UINT16, ZARR_DT_UINT32, ZARR_DT_UINT64,
    ZARR_DT_FLOAT16, ZARR_DT_FLOAT32, ZARR_DT_FLOAT64,
    ZARR_DT_COMPLEX64, ZARR_DT_COMPLEX128,
    ZARR_DT_UNKNOWN,
} zarr__dtype;

typedef enum {
    ZARR_COMP_NONE,
    ZARR_COMP_BLOSC,
    ZARR_COMP_ZLIB,
    ZARR_COMP_ZSTD,
    ZARR_COMP_LZ4,
    ZARR_COMP_GZIP,
    ZARR_COMP_UNKNOWN,
} zarr__compressor;

typedef struct {
    zarr__format     format;
    int8_t           ndim;
    int64_t          shape[ZARR_MAX_DIM];
    int64_t          chunks[ZARR_MAX_DIM];
    zarr__dtype      dtype;
    int              dtype_size;    // bytes per element
    bool             little_endian;
    char             order;        // 'C' or 'F'
    zarr__compressor compressor;
    int              clevel;
    char             blosc_cname[16];   // inner blosc compressor name
    int              blosc_shuffle;     // 0=none, 1=byte, 2=bit
    int              blosc_blocksize;
    char             dim_separator;     // '.' or '/'
    bool             v2_key_encoding;   // v3 with v2-style chunk keys
    uint8_t          fill_value;        // converted to u8 for our use
    bool             sharded;
    int64_t          shard_shape[ZARR_MAX_DIM];
    int64_t          inner_chunks[ZARR_MAX_DIM];
    zarr__compressor inner_compressor;  // compressor for inner chunks in shards
    int              inner_clevel;
    bool             index_at_start;    // shard index location: true=start, false=end
    bool             has_transpose;
    int              transpose_order[ZARR_MAX_DIM];
    bool             has_delta_filter;  // v2 delta filter
} zarr__meta;

static int zarr__dtype_size(zarr__dtype dt) {
    switch (dt) {
        case ZARR_DT_BOOL:    return 1;
        case ZARR_DT_INT8:    return 1;
        case ZARR_DT_INT16:   return 2;
        case ZARR_DT_INT32:   return 4;
        case ZARR_DT_INT64:   return 8;
        case ZARR_DT_UINT8:   return 1;
        case ZARR_DT_UINT16:  return 2;
        case ZARR_DT_UINT32:  return 4;
        case ZARR_DT_UINT64:  return 8;
        case ZARR_DT_FLOAT16: return 2;
        case ZARR_DT_FLOAT32:    return 4;
        case ZARR_DT_FLOAT64:    return 8;
        case ZARR_DT_COMPLEX64:  return 8;
        case ZARR_DT_COMPLEX128: return 16;
        default:                 return 0;
    }
}

// Parse numpy dtype string like "<f4", "|u1", ">i2"
static zarr__dtype zarr__parse_numpy_dtype(const char* s, bool* little_endian, int* size) {
    if (!s || strlen(s) < 2) return ZARR_DT_UNKNOWN;

    char order = s[0];
    char kind = s[1];
    int sz = atoi(s + 2);

    *little_endian = (order == '<' || order == '|' || order == '=');
    *size = sz;

    if (kind == 'b' && sz == 1) return ZARR_DT_BOOL;
    if (kind == 'u') {
        if (sz == 1) return ZARR_DT_UINT8;
        if (sz == 2) return ZARR_DT_UINT16;
        if (sz == 4) return ZARR_DT_UINT32;
        if (sz == 8) return ZARR_DT_UINT64;
    }
    if (kind == 'i') {
        if (sz == 1) return ZARR_DT_INT8;
        if (sz == 2) return ZARR_DT_INT16;
        if (sz == 4) return ZARR_DT_INT32;
        if (sz == 8) return ZARR_DT_INT64;
    }
    if (kind == 'f') {
        if (sz == 2) return ZARR_DT_FLOAT16;
        if (sz == 4) return ZARR_DT_FLOAT32;
        if (sz == 8) return ZARR_DT_FLOAT64;
    }
    if (kind == 'c') {
        if (sz == 8) return ZARR_DT_COMPLEX64;   // 2 x float32
        if (sz == 16) return ZARR_DT_COMPLEX128;  // 2 x float64
    }
    return ZARR_DT_UNKNOWN;
}

// Parse zarr v3 data_type string like "uint8", "float32"
static zarr__dtype zarr__parse_v3_dtype(const char* s, bool* little_endian, int* size) {
    *little_endian = true; // v3 default
    struct { const char* name; zarr__dtype dt; int sz; } table[] = {
        {"bool",    ZARR_DT_BOOL,    1},
        {"int8",    ZARR_DT_INT8,    1},
        {"int16",   ZARR_DT_INT16,   2},
        {"int32",   ZARR_DT_INT32,   4},
        {"int64",   ZARR_DT_INT64,   8},
        {"uint8",   ZARR_DT_UINT8,   1},
        {"uint16",  ZARR_DT_UINT16,  2},
        {"uint32",  ZARR_DT_UINT32,  4},
        {"uint64",  ZARR_DT_UINT64,  8},
        {"float16",    ZARR_DT_FLOAT16,    2},
        {"float32",    ZARR_DT_FLOAT32,    4},
        {"float64",    ZARR_DT_FLOAT64,    8},
        {"complex64",  ZARR_DT_COMPLEX64,  8},
        {"complex128", ZARR_DT_COMPLEX128, 16},
    };
    for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
        if (strcmp(s, table[i].name) == 0) {
            *size = table[i].sz;
            return table[i].dt;
        }
    }
    return ZARR_DT_UNKNOWN;
}

// ── Parse Zarr v2 .zarray ───────────────────────────────────────────────────

static zarr_status zarr__parse_v2_meta(const char* json, size_t len, zarr__meta* meta) {
    memset(meta, 0, sizeof(*meta));
    meta->format = ZARR_FMT_V2;
    meta->order = 'C';
    meta->little_endian = true;
    meta->dim_separator = '.';

    zarr__json j = { .data = json, .pos = 0, .len = len };

    // shape
    if (zarr__json_find_key(&j, "shape")) {
        int count = 0;
        if (!zarr__json_int_array(&j, meta->shape, &count, ZARR_MAX_DIM))
            return ZARR_ERR_FORMAT;
        meta->ndim = (int8_t)count;
    } else return ZARR_ERR_FORMAT;

    // chunks
    if (zarr__json_find_key(&j, "chunks")) {
        int count = 0;
        if (!zarr__json_int_array(&j, meta->chunks, &count, ZARR_MAX_DIM))
            return ZARR_ERR_FORMAT;
    } else return ZARR_ERR_FORMAT;

    // dtype
    if (zarr__json_find_key(&j, "dtype")) {
        char dtype_str[64];
        if (!zarr__json_string(&j, dtype_str, sizeof(dtype_str)))
            return ZARR_ERR_FORMAT;
        meta->dtype = zarr__parse_numpy_dtype(dtype_str, &meta->little_endian,
                                               &meta->dtype_size);
        if (meta->dtype == ZARR_DT_UNKNOWN) return ZARR_ERR_DTYPE;
    } else return ZARR_ERR_FORMAT;

    // order
    if (zarr__json_find_key(&j, "order")) {
        char order_str[8];
        if (zarr__json_string(&j, order_str, sizeof(order_str)))
            meta->order = order_str[0];
    }

    // dimension_separator
    if (zarr__json_find_key(&j, "dimension_separator")) {
        char sep[8];
        if (zarr__json_string(&j, sep, sizeof(sep)))
            meta->dim_separator = sep[0];
    }

    // compressor
    if (zarr__json_find_key(&j, "compressor")) {
        zarr__json_skip_ws(&j);
        if (j.pos < j.len && j.data[j.pos] == 'n') {
            meta->compressor = ZARR_COMP_NONE;
            zarr__json_skip_value(&j);
        } else {
            // Parse compressor object — look for "id" field
            size_t save = j.pos;
            if (zarr__json_match(&j, '{')) {
                while (j.pos < j.len) {
                    char key[64];
                    if (!zarr__json_string(&j, key, sizeof(key))) break;
                    if (!zarr__json_match(&j, ':')) break;
                    if (strcmp(key, "id") == 0) {
                        char id[64];
                        if (zarr__json_string(&j, id, sizeof(id))) {
                            if (strcmp(id, "blosc") == 0) meta->compressor = ZARR_COMP_BLOSC;
                            else if (strcmp(id, "zlib") == 0) meta->compressor = ZARR_COMP_ZLIB;
                            else if (strcmp(id, "zstd") == 0) meta->compressor = ZARR_COMP_ZSTD;
                            else if (strcmp(id, "lz4") == 0) meta->compressor = ZARR_COMP_LZ4;
                            else if (strcmp(id, "gzip") == 0) meta->compressor = ZARR_COMP_GZIP;
                            else meta->compressor = ZARR_COMP_UNKNOWN;
                        }
                    } else if (strcmp(key, "clevel") == 0) {
                        int64_t lvl;
                        if (zarr__json_int(&j, &lvl)) meta->clevel = (int)lvl;
                    } else if (strcmp(key, "cname") == 0) {
                        zarr__json_string(&j, meta->blosc_cname, sizeof(meta->blosc_cname));
                    } else if (strcmp(key, "shuffle") == 0) {
                        int64_t sh;
                        if (zarr__json_int(&j, &sh)) meta->blosc_shuffle = (int)sh;
                    } else if (strcmp(key, "blocksize") == 0) {
                        int64_t bs;
                        if (zarr__json_int(&j, &bs)) meta->blosc_blocksize = (int)bs;
                    } else if (strcmp(key, "level") == 0) {
                        int64_t lvl;
                        if (zarr__json_int(&j, &lvl)) meta->clevel = (int)lvl;
                    } else {
                        zarr__json_skip_value(&j);
                    }
                    zarr__json_skip_ws(&j);
                    if (j.pos < j.len && j.data[j.pos] == ',') j.pos++;
                    else break;
                }
            } else {
                j.pos = save;
                zarr__json_skip_value(&j);
            }
        }
    }

    // filters
    if (zarr__json_find_key(&j, "filters")) {
        zarr__json_skip_ws(&j);
        if (j.pos < j.len && j.data[j.pos] != 'n') {
            size_t fstart = j.pos;
            zarr__json_skip_value(&j);
            size_t fend = j.pos;
            if (strstr(json + fstart, "\"delta\"")) {
                meta->has_delta_filter = true;
            }
        } else {
            zarr__json_skip_value(&j);
        }
    }

    // fill_value
    if (zarr__json_find_key(&j, "fill_value")) {
        zarr__json_skip_ws(&j);
        if (j.pos < j.len) {
            char c = j.data[j.pos];
            if (c == 'n') {
                meta->fill_value = 0;
                zarr__json_skip_value(&j);
            } else if (c == '"') {
                char fvs[32];
                zarr__json_string(&j, fvs, sizeof(fvs));
                meta->fill_value = 0;
            } else {
                int64_t fv;
                if (zarr__json_int(&j, &fv)) {
                    meta->fill_value = (uint8_t)(fv < 0 ? 0 : (fv > 255 ? 255 : fv));
                }
            }
        }
    }

    return ZARR_OK;
}

// ── Parse Zarr v3 zarr.json ─────────────────────────────────────────────────

// Parse a compressor from a codec name string
static zarr__compressor zarr__codec_name_to_comp(const char* name) {
    if (strcmp(name, "blosc") == 0) return ZARR_COMP_BLOSC;
    if (strcmp(name, "zstd") == 0) return ZARR_COMP_ZSTD;
    if (strcmp(name, "gzip") == 0) return ZARR_COMP_GZIP;
    if (strcmp(name, "zlib") == 0) return ZARR_COMP_ZLIB;
    if (strcmp(name, "lz4") == 0) return ZARR_COMP_LZ4;
    return ZARR_COMP_NONE;
}

// Extract codec info from a codec sub-JSON string
static void zarr__parse_codec_entry(const char* entry, size_t entry_len,
                                     zarr__meta* meta, bool is_inner) {
    // Find "name" field
    const char* np = strstr(entry, "\"name\"");
    if (!np || (size_t)(np - entry) >= entry_len) return;
    zarr__json nj = { .data = np, .pos = 0, .len = entry_len - (size_t)(np - entry) };
    char key[64], name[64];
    zarr__json_string(&nj, key, sizeof(key));
    zarr__json_match(&nj, ':');
    if (!zarr__json_string(&nj, name, sizeof(name))) return;

    if (strcmp(name, "bytes") == 0) {
        // Check endian
        const char* ep = strstr(entry, "\"endian\"");
        if (ep && (size_t)(ep - entry) < entry_len) {
            zarr__json ej = { .data = ep, .pos = 0, .len = entry_len - (size_t)(ep - entry) };
            char k2[32], endian[16];
            zarr__json_string(&ej, k2, sizeof(k2));
            zarr__json_match(&ej, ':');
            if (zarr__json_string(&ej, endian, sizeof(endian))) {
                meta->little_endian = (strcmp(endian, "little") == 0);
            }
        }
    } else if (strcmp(name, "transpose") == 0) {
        meta->has_transpose = true;
        const char* op = strstr(entry, "\"order\"");
        if (op && (size_t)(op - entry) < entry_len) {
            zarr__json oj = { .data = op, .pos = 0, .len = entry_len - (size_t)(op - entry) };
            char k2[32];
            zarr__json_string(&oj, k2, sizeof(k2));
            zarr__json_match(&oj, ':');
            int count = 0;
            int64_t order[ZARR_MAX_DIM];
            zarr__json_int_array(&oj, order, &count, ZARR_MAX_DIM);
            for (int i = 0; i < count; i++) meta->transpose_order[i] = (int)order[i];
        }
    } else if (strcmp(name, "sharding_indexed") == 0) {
        meta->sharded = true;
        memcpy(meta->shard_shape, meta->chunks, sizeof(meta->chunks));
        // Extract inner chunk_shape
        const char* cs = strstr(entry, "\"chunk_shape\"");
        if (cs && (size_t)(cs - entry) < entry_len) {
            zarr__json cj = { .data = cs, .pos = 0, .len = entry_len - (size_t)(cs - entry) };
            char k2[64];
            zarr__json_string(&cj, k2, sizeof(k2));
            zarr__json_match(&cj, ':');
            int count = 0;
            zarr__json_int_array(&cj, meta->inner_chunks, &count, ZARR_MAX_DIM);
            memcpy(meta->chunks, meta->inner_chunks, sizeof(meta->inner_chunks));
        }
        // Extract index_location
        const char* il = strstr(entry, "\"index_location\"");
        if (il && (size_t)(il - entry) < entry_len) {
            zarr__json ij = { .data = il, .pos = 0, .len = entry_len - (size_t)(il - entry) };
            char k2[32], loc[16];
            zarr__json_string(&ij, k2, sizeof(k2));
            zarr__json_match(&ij, ':');
            if (zarr__json_string(&ij, loc, sizeof(loc))) {
                meta->index_at_start = (strcmp(loc, "start") == 0);
            }
        }
        // Parse inner codecs for compressor
        const char* ic = strstr(entry, "\"codecs\"");
        if (ic && (size_t)(ic - entry) < entry_len) {
            // Find compressor in inner codec chain
            const char* rest = ic;
            size_t rest_len = entry_len - (size_t)(ic - entry);
            for (const char* names[] = {"blosc","zstd","gzip","zlib","lz4",nullptr}; ; ) {
                bool found = false;
                for (int i = 0; names[i]; i++) {
                    char search[32];
                    snprintf(search, sizeof(search), "\"%s\"", names[i]);
                    if (strstr(rest, search)) {
                        meta->inner_compressor = zarr__codec_name_to_comp(names[i]);
                        found = true;
                        break;
                    }
                }
                break;
                (void)found;
            }
        }
    } else {
        // Bytes-to-bytes compressor
        zarr__compressor comp = zarr__codec_name_to_comp(name);
        if (comp != ZARR_COMP_NONE) {
            if (is_inner) {
                meta->inner_compressor = comp;
            } else {
                meta->compressor = comp;
            }
            // Extract level/clevel
            const char* lp = strstr(entry, "\"level\"");
            if (!lp) lp = strstr(entry, "\"clevel\"");
            if (lp && (size_t)(lp - entry) < entry_len) {
                zarr__json lj = { .data = lp, .pos = 0, .len = entry_len - (size_t)(lp - entry) };
                char k2[32];
                zarr__json_string(&lj, k2, sizeof(k2));
                zarr__json_match(&lj, ':');
                int64_t lvl;
                if (zarr__json_int(&lj, &lvl)) {
                    if (is_inner) meta->inner_clevel = (int)lvl;
                    else meta->clevel = (int)lvl;
                }
            }
            // Blosc-specific: cname, shuffle
            if (comp == ZARR_COMP_BLOSC) {
                const char* cn = strstr(entry, "\"cname\"");
                if (cn && (size_t)(cn - entry) < entry_len) {
                    zarr__json cj = { .data = cn, .pos = 0, .len = entry_len - (size_t)(cn - entry) };
                    char k2[32];
                    zarr__json_string(&cj, k2, sizeof(k2));
                    zarr__json_match(&cj, ':');
                    zarr__json_string(&cj, meta->blosc_cname, sizeof(meta->blosc_cname));
                }
                const char* sh = strstr(entry, "\"shuffle\"");
                if (sh && (size_t)(sh - entry) < entry_len) {
                    zarr__json sj = { .data = sh, .pos = 0, .len = entry_len - (size_t)(sh - entry) };
                    char k2[32], shuf[32];
                    zarr__json_string(&sj, k2, sizeof(k2));
                    zarr__json_match(&sj, ':');
                    if (zarr__json_string(&sj, shuf, sizeof(shuf))) {
                        if (strcmp(shuf, "shuffle") == 0) meta->blosc_shuffle = 1;
                        else if (strcmp(shuf, "bitshuffle") == 0) meta->blosc_shuffle = 2;
                    }
                }
            }
        }
        // crc32c — just ignore (we don't verify checksums on read)
    }
}

// Walk a codecs array and parse each entry
static void zarr__parse_codecs_array(const char* codecs, size_t codecs_len,
                                      zarr__meta* meta, bool is_inner) {
    // codecs is a JSON array: [{...}, {...}, ...]
    // Walk through and find each top-level object
    int depth = 0;
    size_t obj_start = 0;
    bool in_string = false;
    for (size_t i = 0; i < codecs_len; i++) {
        char c = codecs[i];
        if (in_string) {
            if (c == '\\') { i++; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '[' || c == '{') {
            depth++;
            if (depth == 2 && c == '{') obj_start = i;  // top-level object in array (depth: 1=array, 2=object)
        } else if (c == ']' || c == '}') {
            if (depth == 2 && c == '}') {
                zarr__parse_codec_entry(codecs + obj_start, i - obj_start + 1, meta, is_inner);
            }
            depth--;
        }
    }
}

static zarr_status zarr__parse_v3_meta(const char* json, size_t len, zarr__meta* meta) {
    memset(meta, 0, sizeof(*meta));
    meta->format = ZARR_FMT_V3;
    meta->order = 'C';
    meta->little_endian = true;
    meta->dim_separator = '/';

    zarr__json j = { .data = json, .pos = 0, .len = len };

    // shape
    if (zarr__json_find_key(&j, "shape")) {
        int count = 0;
        if (!zarr__json_int_array(&j, meta->shape, &count, ZARR_MAX_DIM))
            return ZARR_ERR_FORMAT;
        meta->ndim = (int8_t)count;
    } else return ZARR_ERR_FORMAT;

    // data_type
    if (zarr__json_find_key(&j, "data_type")) {
        char dt_str[64];
        if (!zarr__json_string(&j, dt_str, sizeof(dt_str)))
            return ZARR_ERR_FORMAT;
        meta->dtype = zarr__parse_v3_dtype(dt_str, &meta->little_endian,
                                            &meta->dtype_size);
        if (meta->dtype == ZARR_DT_UNKNOWN) return ZARR_ERR_DTYPE;
    } else return ZARR_ERR_FORMAT;

    // chunk_grid → regular → chunk_shape
    if (zarr__json_find_key(&j, "chunk_grid")) {
        zarr__json_skip_ws(&j);
        size_t start = j.pos;
        zarr__json_skip_value(&j);
        size_t end = j.pos;
        const char* cs = strstr(json + start, "\"chunk_shape\"");
        if (cs && (size_t)(cs - json) < end) {
            zarr__json csj = { .data = cs, .pos = 0, .len = end - (size_t)(cs - json) };
            char key[64];
            zarr__json_string(&csj, key, sizeof(key));
            zarr__json_match(&csj, ':');
            int count = 0;
            if (!zarr__json_int_array(&csj, meta->chunks, &count, ZARR_MAX_DIM))
                return ZARR_ERR_FORMAT;
        } else return ZARR_ERR_FORMAT;
    } else return ZARR_ERR_FORMAT;

    // chunk_key_encoding — detect "v2" encoding
    if (zarr__json_find_key(&j, "chunk_key_encoding")) {
        zarr__json_skip_ws(&j);
        size_t start = j.pos;
        zarr__json_skip_value(&j);
        size_t end = j.pos;
        // Check for "v2" name
        if (strstr(json + start, "\"v2\"")) {
            meta->v2_key_encoding = true;
            meta->dim_separator = '.';
        }
        // Check for custom separator
        const char* sep = strstr(json + start, "\"separator\"");
        if (sep && (size_t)(sep - json) < end) {
            zarr__json sj = { .data = sep, .pos = 0, .len = end - (size_t)(sep - json) };
            char k[32], sv[8];
            zarr__json_string(&sj, k, sizeof(k));
            zarr__json_match(&sj, ':');
            if (zarr__json_string(&sj, sv, sizeof(sv))) {
                meta->dim_separator = sv[0];
            }
        }
    }

    // fill_value
    if (zarr__json_find_key(&j, "fill_value")) {
        zarr__json_skip_ws(&j);
        if (j.pos < j.len && j.data[j.pos] == 'n') {
            meta->fill_value = 0; // null → 0
            zarr__json_skip_value(&j);
        } else {
            int64_t fv;
            if (zarr__json_int(&j, &fv)) {
                meta->fill_value = (uint8_t)(fv < 0 ? 0 : (fv > 255 ? 255 : fv));
            }
        }
    }

    // codecs — full codec chain parsing
    if (zarr__json_find_key(&j, "codecs")) {
        zarr__json_skip_ws(&j);
        size_t start = j.pos;
        zarr__json_skip_value(&j);
        size_t end = j.pos;
        zarr__parse_codecs_array(json + start, end - start, meta, false);
    }

    return ZARR_OK;
}

// ── Dtype Conversion to uint8 ───────────────────────────────────────────────

static void zarr__byteswap(uint8_t* data, size_t count, int size) {
    for (size_t i = 0; i < count; i++) {
        uint8_t* p = data + i * size;
        for (int j = 0; j < size / 2; j++) {
            uint8_t tmp = p[j];
            p[j] = p[size - 1 - j];
            p[size - 1 - j] = tmp;
        }
    }
}

static void zarr__convert_to_u8(const uint8_t* src, uint8_t* dst,
                                 size_t count, zarr__dtype dtype,
                                 bool src_little_endian) {
    // Work buffer for endian swap if needed
    bool need_swap = false;
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    need_swap = src_little_endian;
    #else
    need_swap = !src_little_endian;
    #endif

    int dsz = zarr__dtype_size(dtype);
    uint8_t* work = nullptr;
    if (need_swap && dsz > 1) {
        work = ZARR_MALLOC(count * dsz);
        if (work) {
            memcpy(work, src, count * dsz);
            zarr__byteswap(work, count, dsz);
            src = work;
        }
    }

    switch (dtype) {
        case ZARR_DT_UINT8:
        case ZARR_DT_BOOL:
            memcpy(dst, src, count);
            break;
        case ZARR_DT_INT8:
            for (size_t i = 0; i < count; i++) {
                int8_t v = (int8_t)src[i];
                dst[i] = v < 0 ? 0 : (uint8_t)v;
            }
            break;
        case ZARR_DT_UINT16:
            for (size_t i = 0; i < count; i++) {
                uint16_t v;
                memcpy(&v, src + i * 2, 2);
                dst[i] = v > 255 ? 255 : (uint8_t)v;
            }
            break;
        case ZARR_DT_INT16:
            for (size_t i = 0; i < count; i++) {
                int16_t v;
                memcpy(&v, src + i * 2, 2);
                dst[i] = v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
            }
            break;
        case ZARR_DT_UINT32:
            for (size_t i = 0; i < count; i++) {
                uint32_t v;
                memcpy(&v, src + i * 4, 4);
                dst[i] = v > 255 ? 255 : (uint8_t)v;
            }
            break;
        case ZARR_DT_INT32:
            for (size_t i = 0; i < count; i++) {
                int32_t v;
                memcpy(&v, src + i * 4, 4);
                dst[i] = v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
            }
            break;
        case ZARR_DT_UINT64:
            for (size_t i = 0; i < count; i++) {
                uint64_t v;
                memcpy(&v, src + i * 8, 8);
                dst[i] = v > 255 ? 255 : (uint8_t)v;
            }
            break;
        case ZARR_DT_INT64:
            for (size_t i = 0; i < count; i++) {
                int64_t v;
                memcpy(&v, src + i * 8, 8);
                dst[i] = v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
            }
            break;
        case ZARR_DT_FLOAT16: {
            // f16 → f32 → u8
            for (size_t i = 0; i < count; i++) {
                uint16_t h;
                memcpy(&h, src + i * 2, 2);
                // IEEE 754 half-precision decode
                uint32_t sign = (h >> 15) & 1;
                uint32_t exp = (h >> 10) & 0x1f;
                uint32_t frac = h & 0x3ff;
                float f;
                if (exp == 0) {
                    f = ldexpf((float)frac, -24); // subnormal
                } else if (exp == 31) {
                    f = frac ? NAN : INFINITY;
                } else {
                    f = ldexpf((float)(frac + 1024), (int)exp - 25);
                }
                if (sign) f = -f;
                int32_t v = (int32_t)roundf(f);
                dst[i] = v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
            }
            break;
        }
        case ZARR_DT_FLOAT32:
            for (size_t i = 0; i < count; i++) {
                float v;
                memcpy(&v, src + i * 4, 4);
                if (isnan(v) || isinf(v)) { dst[i] = 0; continue; }
                int32_t iv = (int32_t)roundf(v);
                dst[i] = iv < 0 ? 0 : (iv > 255 ? 255 : (uint8_t)iv);
            }
            break;
        case ZARR_DT_FLOAT64:
            for (size_t i = 0; i < count; i++) {
                double v;
                memcpy(&v, src + i * 8, 8);
                if (isnan(v) || isinf(v)) { dst[i] = 0; continue; }
                int64_t iv = (int64_t)round(v);
                dst[i] = iv < 0 ? 0 : (iv > 255 ? 255 : (uint8_t)iv);
            }
            break;
        case ZARR_DT_COMPLEX64:
            // complex64 = 2 x float32 — use magnitude
            for (size_t i = 0; i < count; i++) {
                float re, im;
                memcpy(&re, src + i * 8, 4);
                memcpy(&im, src + i * 8 + 4, 4);
                float mag = sqrtf(re * re + im * im);
                if (isnan(mag) || isinf(mag)) { dst[i] = 0; continue; }
                int32_t iv = (int32_t)roundf(mag);
                dst[i] = iv < 0 ? 0 : (iv > 255 ? 255 : (uint8_t)iv);
            }
            break;
        case ZARR_DT_COMPLEX128:
            // complex128 = 2 x float64 — use magnitude
            for (size_t i = 0; i < count; i++) {
                double re, im;
                memcpy(&re, src + i * 16, 8);
                memcpy(&im, src + i * 16 + 8, 8);
                double mag = sqrt(re * re + im * im);
                if (isnan(mag) || isinf(mag)) { dst[i] = 0; continue; }
                int64_t iv = (int64_t)round(mag);
                dst[i] = iv < 0 ? 0 : (iv > 255 ? 255 : (uint8_t)iv);
            }
            break;
        default:
            memset(dst, 0, count);
            break;
    }

    if (work) ZARR_FREE(work);
}

// ── F-order Transpose ───────────────────────────────────────────────────────

// Transpose a 3D buffer from Fortran order (column-major) to C order (row-major).
// For 3D: F-order stores [x fastest, then y, then z] → C-order [z fastest... wait no]
// F-order: data[x * sy*sz + y * sz + z] = voxel(z,y,x) in C indexing
// We need: data_c[z * sy*sx + y * sx + x] = data_f[x * sy*sz + y * sz + z]
static void zarr__transpose_f_to_c(const uint8_t* f_buf, uint8_t* c_buf,
                                    int8_t ndim, const int64_t* shape) {
    if (ndim != 3) {
        // For non-3D, just copy (F-order transpose is complex for arbitrary dims)
        int64_t n = 1;
        for (int8_t i = 0; i < ndim; i++) n *= shape[i];
        memcpy(c_buf, f_buf, (size_t)n);
        return;
    }
    int64_t sz = shape[0], sy = shape[1], sx = shape[2];
    for (int64_t z = 0; z < sz; z++) {
        for (int64_t y = 0; y < sy; y++) {
            for (int64_t x = 0; x < sx; x++) {
                // F-order index: x * (sy*sz) + y * sz + z
                // C-order index: z * (sy*sx) + y * sx + x
                c_buf[z * sy * sx + y * sx + x] = f_buf[x * sy * sz + y * sz + z];
            }
        }
    }
}

// ── Standalone Decompression ─────────────────────────────────────────────────

static uint8_t* zarr__decompress_zstd(const uint8_t* src, size_t src_len,
                                       size_t dst_cap, size_t* out_len) {
    uint8_t* dst = ZARR_MALLOC(dst_cap);
    if (!dst) return nullptr;
    size_t rc = ZSTD_decompress(dst, dst_cap, src, src_len);
    if (ZSTD_isError(rc)) { ZARR_FREE(dst); return nullptr; }
    *out_len = rc;
    return dst;
}

static uint8_t* zarr__decompress_gzip(const uint8_t* src, size_t src_len,
                                       size_t dst_cap, size_t* out_len) {
    uint8_t* dst = ZARR_MALLOC(dst_cap);
    if (!dst) return nullptr;
    z_stream strm = {0};
    strm.next_in = (Bytef*)src;
    strm.avail_in = (uInt)src_len;
    strm.next_out = dst;
    strm.avail_out = (uInt)dst_cap;
    // windowBits=15+32 for auto-detect gzip/zlib
    if (inflateInit2(&strm, 15 + 32) != Z_OK) { ZARR_FREE(dst); return nullptr; }
    int ret = inflate(&strm, Z_FINISH);
    size_t written = strm.total_out;
    inflateEnd(&strm);
    if (ret != Z_STREAM_END) { ZARR_FREE(dst); return nullptr; }
    *out_len = written;
    return dst;
}

static uint8_t* zarr__decompress_lz4(const uint8_t* src, size_t src_len,
                                      size_t dst_cap, size_t* out_len) {
    uint8_t* dst = ZARR_MALLOC(dst_cap);
    if (!dst) return nullptr;
    int rc = LZ4_decompress_safe((const char*)src, (char*)dst,
                                  (int)src_len, (int)dst_cap);
    if (rc < 0) { ZARR_FREE(dst); return nullptr; }
    *out_len = (size_t)rc;
    return dst;
}

// ── Foreign Chunk Reader ────────────────────────────────────────────────────

// Read a raw chunk file (may be compressed) and return decompressed data.
// Caller must free returned buffer.
static uint8_t* zarr__read_raw_chunk(const char* path, const zarr__meta* meta,
                                      size_t* out_len) {
    size_t flen;
    uint8_t* raw = zarr__read_file_raw(path, &flen, 0);
    if (!raw) return nullptr;

    // Compute expected decompressed size
    size_t elem_count = 1;
    for (int8_t i = 0; i < meta->ndim; i++) {
        elem_count *= (size_t)meta->chunks[i];
    }
    size_t decompressed_size = elem_count * meta->dtype_size;

    if (meta->compressor == ZARR_COMP_NONE) {
        *out_len = flen;
        return raw;
    }

    uint8_t* result = nullptr;

    if (meta->compressor == ZARR_COMP_BLOSC) {
        result = ZARR_MALLOC(decompressed_size);
        if (!result) { ZARR_FREE(raw); return nullptr; }
        int rc = blosc2_decompress(raw, (int32_t)flen, result, (int32_t)decompressed_size);
        ZARR_FREE(raw);
        if (rc < 0) { ZARR_FREE(result); return nullptr; }
        *out_len = (size_t)rc;
        return result;
    }

    if (meta->compressor == ZARR_COMP_ZSTD) {
        result = zarr__decompress_zstd(raw, flen, decompressed_size, out_len);
        ZARR_FREE(raw);
        return result;
    }

    if (meta->compressor == ZARR_COMP_ZLIB || meta->compressor == ZARR_COMP_GZIP) {
        result = zarr__decompress_gzip(raw, flen, decompressed_size, out_len);
        ZARR_FREE(raw);
        return result;
    }

    if (meta->compressor == ZARR_COMP_LZ4) {
        result = zarr__decompress_lz4(raw, flen, decompressed_size, out_len);
        ZARR_FREE(raw);
        return result;
    }

    // Unknown compressor — return raw and hope for the best
    *out_len = flen;
    return raw;
}

// ── Zarr v3 Shard Reader ────────────────────────────────────────────────────

// Shard index entry: offset + nbytes, both uint64
#define ZARR_SHARD_EMPTY UINT64_MAX

typedef struct {
    uint64_t offset;
    uint64_t nbytes;
} zarr__shard_entry;

static zarr_status zarr__read_shard_index(const char* shard_path,
                                           int8_t ndim,
                                           const int64_t* chunks_per_shard,
                                           bool index_at_start,
                                           zarr__shard_entry** out_index,
                                           int64_t* out_nentries) {
    int64_t n = 1;
    for (int8_t i = 0; i < ndim; i++) n *= chunks_per_shard[i];

    size_t index_bytes = (size_t)n * 16;

    FILE* f = fopen(shard_path, "rb");
    if (!f) return ZARR_ERR_IO;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < (long)index_bytes) { fclose(f); return ZARR_ERR_FORMAT; }

    if (index_at_start) {
        fseek(f, 0, SEEK_SET);
    } else {
        fseek(f, fsize - (long)index_bytes, SEEK_SET);
    }

    zarr__shard_entry* idx = ZARR_MALLOC(index_bytes);
    if (!idx) { fclose(f); return ZARR_ERR_ALLOC; }

    size_t rd = fread(idx, 1, index_bytes, f);
    fclose(f);
    if (rd != index_bytes) { ZARR_FREE(idx); return ZARR_ERR_IO; }

    // Index is stored as little-endian uint64 pairs
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (int64_t i = 0; i < n; i++) {
        zarr__byteswap((uint8_t*)&idx[i].offset, 1, 8);
        zarr__byteswap((uint8_t*)&idx[i].nbytes, 1, 8);
    }
    #endif

    *out_index = idx;
    *out_nentries = n;
    return ZARR_OK;
}

static uint8_t* zarr__read_shard_chunk(const char* shard_path,
                                        const zarr__shard_entry* entry,
                                        size_t* out_len) {
    if (entry->offset == ZARR_SHARD_EMPTY) return nullptr;

    FILE* f = fopen(shard_path, "rb");
    if (!f) return nullptr;

    fseek(f, (long)entry->offset, SEEK_SET);
    uint8_t* buf = ZARR_MALLOC((size_t)entry->nbytes);
    if (!buf) { fclose(f); return nullptr; }

    size_t rd = fread(buf, 1, (size_t)entry->nbytes, f);
    fclose(f);
    if (rd != (size_t)entry->nbytes) { ZARR_FREE(buf); return nullptr; }

    *out_len = (size_t)entry->nbytes;
    return buf;
}

// ── Ingest Pipeline ─────────────────────────────────────────────────────────

// Build chunk file path for zarr v2: base/z.y.x or base/z/y/x
static void zarr__v2_chunk_path(char* out, size_t maxlen, const char* base,
                                 int8_t ndim, const int64_t* idx, char sep) {
    int pos = snprintf(out, maxlen, "%s/", base);
    for (int8_t i = 0; i < ndim; i++) {
        if (i > 0) pos += snprintf(out + pos, maxlen - pos, "%c", sep);
        pos += snprintf(out + pos, maxlen - pos, "%lld", (long long)idx[i]);
    }
}

// Build chunk file path for zarr v3: base/c/z/y/x
static void zarr__v3_chunk_path(char* out, size_t maxlen, const char* base,
                                 int8_t ndim, const int64_t* idx) {
    int pos = snprintf(out, maxlen, "%s/c", base);
    for (int8_t i = 0; i < ndim; i++) {
        pos += snprintf(out + pos, maxlen - pos, "/%lld", (long long)idx[i]);
    }
}

// Build shard file path for zarr v3 sharded: base/c/sz/sy/sx
static void zarr__v3_shard_path(char* out, size_t maxlen, const char* base,
                                 int8_t ndim, const int64_t* shard_idx) {
    zarr__v3_chunk_path(out, maxlen, base, ndim, shard_idx);
}

// Iterate over all chunks in a region and write to destination array
static zarr_status zarr__ingest_unsharded(zarr_array* dst, const char* src_path,
                                           const zarr__meta* meta) {
    // Compute number of chunks per dimension
    int64_t nchunks[ZARR_MAX_DIM];
    for (int8_t i = 0; i < meta->ndim; i++) {
        nchunks[i] = (meta->shape[i] + meta->chunks[i] - 1) / meta->chunks[i];
    }

    // Allocate conversion buffers
    size_t src_chunk_elems = 1;
    for (int8_t i = 0; i < meta->ndim; i++) src_chunk_elems *= (size_t)meta->chunks[i];
    uint8_t* u8_buf = ZARR_MALLOC(src_chunk_elems);
    if (!u8_buf) return ZARR_ERR_ALLOC;

    // Iterate all chunks (row-major order)
    int64_t idx[ZARR_MAX_DIM] = {0};
    bool done = false;
    while (!done) {
        // Build source chunk path
        char cpath[ZARR_PATH_MAX];
        if (meta->format == ZARR_FMT_V2)
            zarr__v2_chunk_path(cpath, sizeof(cpath), src_path, meta->ndim, idx, meta->dim_separator);
        else if (meta->v2_key_encoding)
            zarr__v2_chunk_path(cpath, sizeof(cpath), src_path, meta->ndim, idx, meta->dim_separator);
        else
            zarr__v3_chunk_path(cpath, sizeof(cpath), src_path, meta->ndim, idx);

        // Compute destination region
        int64_t start[ZARR_MAX_DIM], stop[ZARR_MAX_DIM];
        int64_t buffershape[ZARR_MAX_DIM];
        size_t region_elems = 1;
        for (int8_t i = 0; i < meta->ndim; i++) {
            start[i] = idx[i] * meta->chunks[i];
            stop[i] = start[i] + meta->chunks[i];
            if (stop[i] > meta->shape[i]) stop[i] = meta->shape[i];
            buffershape[i] = stop[i] - start[i];
            region_elems *= (size_t)buffershape[i];
        }

        if (zarr__file_exists(cpath)) {
            size_t raw_len;
            uint8_t* raw = zarr__read_raw_chunk(cpath, meta, &raw_len);
            if (raw) {
                // Delta filter: undo cumulative delta encoding
                if (meta->has_delta_filter && raw_len > 0) {
                    for (size_t di = 1; di < raw_len; di++) {
                        raw[di] = (uint8_t)(raw[di] + raw[di - 1]);
                    }
                }

                // F-order transpose if needed (before dtype conversion)
                if (meta->order == 'F' && meta->ndim == 3) {
                    size_t raw_bytes = region_elems * meta->dtype_size;
                    uint8_t* transposed = ZARR_MALLOC(raw_bytes);
                    if (transposed) {
                        zarr__transpose_f_to_c(raw, transposed, meta->ndim, buffershape);
                        ZARR_FREE(raw);
                        raw = transposed;
                    }
                }
                zarr__convert_to_u8(raw, u8_buf, region_elems, meta->dtype,
                                     meta->little_endian);
                ZARR_FREE(raw);

                int rc = b2nd_set_slice_cbuffer(u8_buf, buffershape,
                                                (int64_t)region_elems,
                                                start, stop, dst->b2);
                if (rc < 0) {
                    ZARR_FREE(u8_buf);
                    return ZARR_ERR_BLOSC;
                }
            }
        }

        // Advance to next chunk index (row-major)
        for (int8_t d = meta->ndim - 1; d >= 0; d--) {
            idx[d]++;
            if (idx[d] < nchunks[d]) break;
            idx[d] = 0;
            if (d == 0) done = true;
        }
    }

    ZARR_FREE(u8_buf);
    return ZARR_OK;
}

static zarr_status zarr__ingest_sharded(zarr_array* dst, const char* src_path,
                                         const zarr__meta* meta) {
    // Compute shards and chunks per shard
    int64_t nshards[ZARR_MAX_DIM];
    int64_t chunks_per_shard[ZARR_MAX_DIM];
    for (int8_t i = 0; i < meta->ndim; i++) {
        chunks_per_shard[i] = meta->shard_shape[i] / meta->inner_chunks[i];
        nshards[i] = (meta->shape[i] + meta->shard_shape[i] - 1) / meta->shard_shape[i];
    }

    size_t inner_elems = 1;
    for (int8_t i = 0; i < meta->ndim; i++) inner_elems *= (size_t)meta->inner_chunks[i];

    uint8_t* u8_buf = ZARR_MALLOC(inner_elems);
    if (!u8_buf) return ZARR_ERR_ALLOC;

    // Iterate all shards
    int64_t shard_idx[ZARR_MAX_DIM] = {0};
    bool shard_done = false;
    while (!shard_done) {
        char spath[ZARR_PATH_MAX];
        zarr__v3_shard_path(spath, sizeof(spath), src_path, meta->ndim, shard_idx);

        if (zarr__file_exists(spath)) {
            zarr__shard_entry* index = nullptr;
            int64_t nentries;
            zarr_status s = zarr__read_shard_index(spath, meta->ndim,
                                                    chunks_per_shard,
                                                    meta->index_at_start,
                                                    &index, &nentries);
            if (s != ZARR_OK) {
                ZARR_FREE(u8_buf);
                return s;
            }

            // Iterate inner chunks within this shard
            int64_t inner_idx[ZARR_MAX_DIM] = {0};
            for (int64_t ci = 0; ci < nentries; ci++) {
                if (index[ci].offset != ZARR_SHARD_EMPTY) {
                    size_t clen;
                    uint8_t* cdata = zarr__read_shard_chunk(spath, &index[ci], &clen);
                    if (cdata) {
                        size_t raw_size = inner_elems * meta->dtype_size;
                        uint8_t* decoded = nullptr;
                        size_t decoded_len = 0;

                        // Decompress inner chunk based on inner_compressor
                        switch (meta->inner_compressor) {
                            case ZARR_COMP_BLOSC: {
                                decoded = ZARR_MALLOC(raw_size);
                                if (decoded) {
                                    int rc = blosc2_decompress(cdata, (int32_t)clen,
                                                               decoded, (int32_t)raw_size);
                                    if (rc < 0) { ZARR_FREE(decoded); decoded = nullptr; }
                                    else decoded_len = (size_t)rc;
                                }
                                break;
                            }
                            case ZARR_COMP_ZSTD:
                                decoded = zarr__decompress_zstd(cdata, clen, raw_size, &decoded_len);
                                break;
                            case ZARR_COMP_GZIP:
                            case ZARR_COMP_ZLIB:
                                decoded = zarr__decompress_gzip(cdata, clen, raw_size, &decoded_len);
                                break;
                            case ZARR_COMP_LZ4:
                                decoded = zarr__decompress_lz4(cdata, clen, raw_size, &decoded_len);
                                break;
                            default:
                                // No compression — use raw data directly
                                decoded = cdata;
                                decoded_len = clen;
                                cdata = nullptr;  // prevent double-free
                                break;
                        }

                        if (decoded) {
                            zarr__convert_to_u8(decoded, u8_buf, inner_elems,
                                                 meta->dtype, meta->little_endian);
                            if (decoded != cdata) ZARR_FREE(decoded);
                        }
                        ZARR_FREE(cdata);

                        // Compute global position
                        int64_t start[ZARR_MAX_DIM], stop[ZARR_MAX_DIM];
                        int64_t buffershape[ZARR_MAX_DIM];
                        size_t region = 1;
                        for (int8_t d = 0; d < meta->ndim; d++) {
                            start[d] = shard_idx[d] * meta->shard_shape[d]
                                     + inner_idx[d] * meta->inner_chunks[d];
                            stop[d] = start[d] + meta->inner_chunks[d];
                            if (stop[d] > meta->shape[d]) stop[d] = meta->shape[d];
                            buffershape[d] = stop[d] - start[d];
                            region *= (size_t)buffershape[d];
                        }

                        b2nd_set_slice_cbuffer(u8_buf, buffershape,
                                               (int64_t)region, start, stop, dst->b2);
                    }
                }

                // Advance inner chunk index (row-major)
                for (int8_t d = meta->ndim - 1; d >= 0; d--) {
                    inner_idx[d]++;
                    if (inner_idx[d] < chunks_per_shard[d]) break;
                    inner_idx[d] = 0;
                }
            }
            ZARR_FREE(index);
        }

        // Advance shard index
        for (int8_t d = meta->ndim - 1; d >= 0; d--) {
            shard_idx[d]++;
            if (shard_idx[d] < nshards[d]) break;
            shard_idx[d] = 0;
            if (d == 0) shard_done = true;
        }
    }

    ZARR_FREE(u8_buf);
    return ZARR_OK;
}

zarr_status zarr_ingest(zarr_array** out, const char* src_path,
                        const char* cache_path) {
    if (!out || !src_path || !cache_path) return ZARR_ERR_NULL_ARG;
    if (!g_initialized) return ZARR_ERR_BLOSC;

    // If cache already exists, just open it
    if (zarr__is_dir(cache_path)) {
        return zarr_open(out, cache_path);
    }

    // Detect format
    char meta_path[ZARR_PATH_MAX];
    zarr__meta meta;
    zarr_status s;

    // Try zarr v3 first
    snprintf(meta_path, sizeof(meta_path), "%s/zarr.json", src_path);
    if (zarr__file_exists(meta_path)) {
        size_t mlen;
        char* mjson = zarr__read_file(meta_path, &mlen);
        if (!mjson) return ZARR_ERR_IO;
        s = zarr__parse_v3_meta(mjson, mlen, &meta);
        ZARR_FREE(mjson);
        if (s != ZARR_OK) return s;
    } else {
        // Try zarr v2
        snprintf(meta_path, sizeof(meta_path), "%s/.zarray", src_path);
        if (!zarr__file_exists(meta_path)) return ZARR_ERR_FORMAT;
        size_t mlen;
        char* mjson = zarr__read_file(meta_path, &mlen);
        if (!mjson) return ZARR_ERR_IO;
        s = zarr__parse_v2_meta(mjson, mlen, &meta);
        ZARR_FREE(mjson);
        if (s != ZARR_OK) return s;
    }

    // Create canonical destination array
    zarr_array* dst = nullptr;
    s = zarr_create(&dst, cache_path, meta.ndim, meta.shape);
    if (s != ZARR_OK) return s;

    // Ingest data
    if (meta.sharded) {
        s = zarr__ingest_sharded(dst, src_path, &meta);
    } else {
        s = zarr__ingest_unsharded(dst, src_path, &meta);
    }

    if (s != ZARR_OK) {
        zarr_close(dst);
        return s;
    }

    *out = dst;
    return ZARR_OK;
}

// ── Statistics ──────────────────────────────────────────────────────────────

zarr_status zarr_compute_stats(const zarr_array* arr,
                               const int64_t* start,
                               const int64_t* stop,
                               zarr_stats* stats) {
    if (!arr || !start || !stop || !stats) return ZARR_ERR_NULL_ARG;

    int64_t n = zarr__region_size(arr->ndim, start, stop);
    if (n <= 0) return ZARR_ERR_BOUNDS;

    uint8_t* buf = ZARR_MALLOC((size_t)n);
    if (!buf) return ZARR_ERR_ALLOC;

    zarr_status s = zarr_read(arr, start, stop, buf, n);
    if (s != ZARR_OK) { ZARR_FREE(buf); return s; }

    uint8_t mn = 255, mx = 0;
    double sum = 0;
    int64_t zeros = 0;
    for (int64_t i = 0; i < n; i++) {
        uint8_t v = buf[i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
        if (v == 0) zeros++;
    }
    double mean = sum / (double)n;

    double var_sum = 0;
    for (int64_t i = 0; i < n; i++) {
        double d = (double)buf[i] - mean;
        var_sum += d * d;
    }

    stats->mean = mean;
    stats->stddev = sqrt(var_sum / (double)n);
    stats->min = mn;
    stats->max = mx;
    stats->count = n;
    stats->zero_count = zeros;

    ZARR_FREE(buf);
    return ZARR_OK;
}

// ── Region Operations ───────────────────────────────────────────────────────

zarr_status zarr_fill(zarr_array* arr,
                      const int64_t* start,
                      const int64_t* stop,
                      uint8_t value) {
    if (!arr || !start || !stop) return ZARR_ERR_NULL_ARG;

    int64_t n = zarr__region_size(arr->ndim, start, stop);
    if (n <= 0) return ZARR_ERR_BOUNDS;

    uint8_t* buf = ZARR_MALLOC((size_t)n);
    if (!buf) return ZARR_ERR_ALLOC;

    memset(buf, value, (size_t)n);
    zarr_status s = zarr_write(arr, start, stop, buf, n);
    ZARR_FREE(buf);
    return s;
}

zarr_status zarr_copy_region(const zarr_array* src,
                             const int64_t* src_start,
                             const int64_t* src_stop,
                             zarr_array* dst,
                             const int64_t* dst_start) {
    if (!src || !src_start || !src_stop || !dst || !dst_start)
        return ZARR_ERR_NULL_ARG;
    if (src->ndim != dst->ndim) return ZARR_ERR_BOUNDS;

    int64_t n = zarr__region_size(src->ndim, src_start, src_stop);
    if (n <= 0) return ZARR_ERR_BOUNDS;

    // Compute dst_stop from src region size
    int64_t dst_stop[ZARR_MAX_DIM];
    for (int8_t i = 0; i < src->ndim; i++) {
        dst_stop[i] = dst_start[i] + (src_stop[i] - src_start[i]);
        if (dst_stop[i] > dst->shape[i]) return ZARR_ERR_BOUNDS;
    }

    uint8_t* buf = ZARR_MALLOC((size_t)n);
    if (!buf) return ZARR_ERR_ALLOC;

    zarr_status s = zarr_read(src, src_start, src_stop, buf, n);
    if (s != ZARR_OK) { ZARR_FREE(buf); return s; }

    s = zarr_write(dst, dst_start, dst_stop, buf, n);
    ZARR_FREE(buf);
    return s;
}

// ── Compression Info ────────────────────────────────────────────────────────

zarr_status zarr_compressed_size(const zarr_array* arr, int64_t* out_bytes) {
    if (!arr || !out_bytes) return ZARR_ERR_NULL_ARG;
    if (!arr->b2 || !arr->b2->sc) return ZARR_ERR_BLOSC;
    *out_bytes = (int64_t)arr->b2->sc->cbytes;
    return ZARR_OK;
}

double zarr_compression_ratio(const zarr_array* arr) {
    if (!arr || !arr->b2 || !arr->b2->sc) return 0.0;
    int64_t cbytes = (int64_t)arr->b2->sc->cbytes;
    if (cbytes <= 0) return 0.0;
    int64_t nbytes = (int64_t)arr->b2->sc->nbytes;
    return (double)nbytes / (double)cbytes;
}

// ── Encoder Configuration ───────────────────────────────────────────────────

zarr_status zarr_set_quality(int quality) {
    if (!g_initialized) return ZARR_ERR_BLOSC;
    if (quality < 0 || quality > 2) return ZARR_ERR_BOUNDS;

    vl264_cfg cfg = vl264_default_cfg();
    cfg.quality = (vl264_quality)quality;

    vl264_enc* new_enc = vl264_enc_create(&cfg);
    if (!new_enc) return ZARR_ERR_VL264;

    if (g_encoder) vl264_enc_destroy(g_encoder);
    g_encoder = new_enc;
    return ZARR_OK;
}

const char* zarr_array_path(const zarr_array* arr) {
    return arr ? arr->path : nullptr;
}

// ── Slice Extraction ────────────────────────────────────────────────────────

zarr_status zarr_slice(const zarr_array* arr, zarr_axis axis, int64_t index,
                       uint8_t* buf, int64_t bufsize) {
    if (!arr || !buf) return ZARR_ERR_NULL_ARG;
    if (arr->ndim != 3) return ZARR_ERR_UNSUPPORTED;
    if (axis < 0 || axis > 2) return ZARR_ERR_BOUNDS;
    if (index < 0 || index >= arr->shape[axis]) return ZARR_ERR_BOUNDS;

    int64_t start[3], stop[3];
    int64_t slice_size = 1;
    for (int8_t i = 0; i < 3; i++) {
        if (i == (int8_t)axis) {
            start[i] = index;
            stop[i] = index + 1;
        } else {
            start[i] = 0;
            stop[i] = arr->shape[i];
            slice_size *= arr->shape[i];
        }
    }
    if (bufsize < slice_size) return ZARR_ERR_BOUNDS;

    return zarr_read(arr, start, stop, buf, bufsize);
}

// ── Downsampling / LOD ──────────────────────────────────────────────────────

zarr_status zarr_downsample_2x(const zarr_array* src, const char* dst_path,
                               zarr_array** out) {
    if (!src || !dst_path || !out) return ZARR_ERR_NULL_ARG;
    if (src->ndim != 3) return ZARR_ERR_UNSUPPORTED;

    int64_t dst_shape[3];
    for (int i = 0; i < 3; i++) {
        dst_shape[i] = (src->shape[i] + 1) / 2;
    }

    zarr_array* dst = nullptr;
    zarr_status s = zarr_create(&dst, dst_path, 3, dst_shape);
    if (s != ZARR_OK) return s;

    // Process chunk-by-chunk in the output array
    int64_t nchunks[3];
    for (int i = 0; i < 3; i++) {
        nchunks[i] = (dst_shape[i] + ZARR_CHUNK_DIM - 1) / ZARR_CHUNK_DIM;
    }

    // We read 256³ regions from src (2x the output chunk) and downsample to 128³
    size_t src_region = 256 * 256 * 256;
    uint8_t* src_buf = ZARR_MALLOC(src_region);
    uint8_t* dst_buf = ZARR_CALLOC(ZARR_CHUNK_VOXELS, 1);
    if (!src_buf || !dst_buf) {
        ZARR_FREE(src_buf);
        ZARR_FREE(dst_buf);
        zarr_close(dst);
        return ZARR_ERR_ALLOC;
    }

    for (int64_t cz = 0; cz < nchunks[0]; cz++) {
        for (int64_t cy = 0; cy < nchunks[1]; cy++) {
            for (int64_t cx = 0; cx < nchunks[2]; cx++) {
                // Source region: 2x the chunk coords
                int64_t src_start[3] = { cz * 256, cy * 256, cx * 256 };
                int64_t src_stop[3];
                int64_t actual[3];
                for (int i = 0; i < 3; i++) {
                    src_stop[i] = src_start[i] + 256;
                    if (src_stop[i] > src->shape[i]) src_stop[i] = src->shape[i];
                    actual[i] = src_stop[i] - src_start[i];
                }

                int64_t src_n = actual[0] * actual[1] * actual[2];
                if (src_n <= 0) continue;

                memset(src_buf, 0, src_region);
                s = zarr_read(src, src_start, src_stop, src_buf, src_n);
                if (s != ZARR_OK) goto cleanup;

                // Downsample 2x with averaging
                int64_t dz_size = (actual[0] + 1) / 2;
                int64_t dy_size = (actual[1] + 1) / 2;
                int64_t dx_size = (actual[2] + 1) / 2;

                memset(dst_buf, 0, ZARR_CHUNK_VOXELS);
                for (int64_t dz = 0; dz < dz_size && dz < 128; dz++) {
                    for (int64_t dy = 0; dy < dy_size && dy < 128; dy++) {
                        for (int64_t dx = 0; dx < dx_size && dx < 128; dx++) {
                            uint32_t sum = 0;
                            int count = 0;
                            for (int oz = 0; oz < 2; oz++) {
                                for (int oy = 0; oy < 2; oy++) {
                                    for (int ox = 0; ox < 2; ox++) {
                                        int64_t sz = dz * 2 + oz;
                                        int64_t sy = dy * 2 + oy;
                                        int64_t sx = dx * 2 + ox;
                                        if (sz < actual[0] && sy < actual[1] && sx < actual[2]) {
                                            sum += src_buf[sz * actual[1] * actual[2] + sy * actual[2] + sx];
                                            count++;
                                        }
                                    }
                                }
                            }
                            dst_buf[dz * 128 * 128 + dy * 128 + dx] =
                                (uint8_t)((sum + count / 2) / count);
                        }
                    }
                }

                // Write the downsampled chunk
                int64_t dst_start[3] = { cz * 128, cy * 128, cx * 128 };
                int64_t dst_stop_r[3];
                int64_t ds[3] = { dz_size, dy_size, dx_size };
                for (int i = 0; i < 3; i++) {
                    if (ds[i] > 128) ds[i] = 128;
                    dst_stop_r[i] = dst_start[i] + ds[i];
                    if (dst_stop_r[i] > dst_shape[i]) dst_stop_r[i] = dst_shape[i];
                }

                int64_t dst_n = 1;
                int64_t dst_bs[3];
                for (int i = 0; i < 3; i++) {
                    dst_bs[i] = dst_stop_r[i] - dst_start[i];
                    dst_n *= dst_bs[i];
                }

                int rc = b2nd_set_slice_cbuffer(dst_buf, dst_bs, dst_n,
                                                dst_start, dst_stop_r, dst->b2);
                if (rc < 0) { s = ZARR_ERR_BLOSC; goto cleanup; }
            }
        }
    }

    ZARR_FREE(src_buf);
    ZARR_FREE(dst_buf);
    *out = dst;
    return ZARR_OK;

cleanup:
    ZARR_FREE(src_buf);
    ZARR_FREE(dst_buf);
    zarr_close(dst);
    return s;
}

// ── Histogram ───────────────────────────────────────────────────────────────

zarr_status zarr_compute_histogram(const zarr_array* arr,
                                   const int64_t* start,
                                   const int64_t* stop,
                                   zarr_histogram* hist) {
    if (!arr || !start || !stop || !hist) return ZARR_ERR_NULL_ARG;

    int64_t n = zarr__region_size(arr->ndim, start, stop);
    if (n <= 0) return ZARR_ERR_BOUNDS;

    uint8_t* buf = ZARR_MALLOC((size_t)n);
    if (!buf) return ZARR_ERR_ALLOC;

    zarr_status s = zarr_read(arr, start, stop, buf, n);
    if (s != ZARR_OK) { ZARR_FREE(buf); return s; }

    memset(hist->bins, 0, sizeof(hist->bins));
    for (int64_t i = 0; i < n; i++) {
        hist->bins[buf[i]]++;
    }
    hist->total = n;

    // Compute percentiles
    uint64_t targets[] = {
        (uint64_t)(n * 0.01),
        (uint64_t)(n * 0.05),
        (uint64_t)(n * 0.50),
        (uint64_t)(n * 0.95),
        (uint64_t)(n * 0.99),
    };
    uint8_t* results[] = {
        &hist->percentile_1,
        &hist->percentile_5,
        &hist->percentile_50,
        &hist->percentile_95,
        &hist->percentile_99,
    };
    uint64_t cumulative = 0;
    int ti = 0;
    for (int v = 0; v < 256 && ti < 5; v++) {
        cumulative += hist->bins[v];
        while (ti < 5 && cumulative > targets[ti]) {
            *results[ti] = (uint8_t)v;
            ti++;
        }
    }
    while (ti < 5) { *results[ti] = 255; ti++; }

    ZARR_FREE(buf);
    return ZARR_OK;
}

// ── Chunk Iteration ─────────────────────────────────────────────────────────

zarr_status zarr_foreach_chunk(const zarr_array* arr, zarr_chunk_fn fn,
                               void* userdata) {
    if (!arr || !fn) return ZARR_ERR_NULL_ARG;

    int64_t nchunks_per[ZARR_MAX_DIM];
    for (int8_t i = 0; i < arr->ndim; i++) {
        nchunks_per[i] = (arr->shape[i] + ZARR_CHUNK_DIM - 1) / ZARR_CHUNK_DIM;
    }

    uint8_t* buf = ZARR_MALLOC(ZARR_CHUNK_VOXELS);
    if (!buf) return ZARR_ERR_ALLOC;

    int64_t coords[ZARR_MAX_DIM] = {0};
    bool done = false;
    while (!done) {
        int64_t start[ZARR_MAX_DIM], stop[ZARR_MAX_DIM];
        int64_t buffershape[ZARR_MAX_DIM];
        int64_t nvoxels = 1;
        for (int8_t i = 0; i < arr->ndim; i++) {
            start[i] = coords[i] * ZARR_CHUNK_DIM;
            stop[i] = start[i] + ZARR_CHUNK_DIM;
            if (stop[i] > arr->shape[i]) stop[i] = arr->shape[i];
            buffershape[i] = stop[i] - start[i];
            nvoxels *= buffershape[i];
        }

        zarr_status s = zarr_read(arr, start, stop, buf, nvoxels);
        if (s != ZARR_OK) { ZARR_FREE(buf); return s; }

        s = fn(coords, buf, nvoxels, userdata);
        if (s != ZARR_OK) { ZARR_FREE(buf); return s; }

        // Advance coords (row-major)
        for (int8_t d = arr->ndim - 1; d >= 0; d--) {
            coords[d]++;
            if (coords[d] < nchunks_per[d]) break;
            coords[d] = 0;
            if (d == 0) done = true;
        }
    }

    ZARR_FREE(buf);
    return ZARR_OK;
}

// ── Resize ──────────────────────────────────────────────────────────────────

zarr_status zarr_resize(zarr_array* arr, const int64_t* new_shape) {
    if (!arr || !new_shape) return ZARR_ERR_NULL_ARG;

    for (int8_t i = 0; i < arr->ndim; i++) {
        if (new_shape[i] <= 0) return ZARR_ERR_BOUNDS;
    }

    // Use b2nd_resize — grow/shrink from the end of each dimension
    int64_t start[ZARR_MAX_DIM];
    for (int8_t i = 0; i < arr->ndim; i++) {
        start[i] = arr->shape[i];  // insert new data after existing
    }
    int rc = b2nd_resize(arr->b2, new_shape, start);
    if (rc < 0) return ZARR_ERR_BLOSC;

    memcpy(arr->shape, new_shape, arr->ndim * sizeof(int64_t));
    return ZARR_OK;
}

// ── Print Info ──────────────────────────────────────────────────────────────

void zarr_print_info(const zarr_array* arr, FILE* out) {
    if (!arr || !out) return;

    fprintf(out, "zarr array: %s\n", arr->path);
    fprintf(out, "  ndim:   %d\n", arr->ndim);
    fprintf(out, "  shape:  [");
    for (int8_t i = 0; i < arr->ndim; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "%lld", (long long)arr->shape[i]);
    }
    fprintf(out, "]\n");
    fprintf(out, "  chunks: [");
    for (int8_t i = 0; i < arr->ndim; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "%d", ZARR_CHUNK_DIM);
    }
    fprintf(out, "]\n");
    fprintf(out, "  dtype:  uint8\n");
    fprintf(out, "  codec:  vl264\n");
    fprintf(out, "  nchunks: %lld\n", (long long)zarr_nchunks(arr));

    int64_t nbytes = 1;
    for (int8_t i = 0; i < arr->ndim; i++) nbytes *= arr->shape[i];
    fprintf(out, "  uncompressed: %.2f MB\n", (double)nbytes / (1024.0 * 1024.0));

    if (arr->b2 && arr->b2->sc) {
        int64_t cbytes = (int64_t)arr->b2->sc->cbytes;
        if (cbytes > 0) {
            fprintf(out, "  compressed:   %.2f MB\n", (double)cbytes / (1024.0 * 1024.0));
            fprintf(out, "  ratio:        %.1fx\n", (double)nbytes / (double)cbytes);
        }
    }
}

// ── Projections ─────────────────────────────────────────────────────────────

static zarr_status zarr__project(const zarr_array* arr, zarr_axis axis,
                                  int64_t start_idx, int64_t stop_idx,
                                  uint8_t* buf, int64_t bufsize, bool do_max) {
    if (!arr || !buf) return ZARR_ERR_NULL_ARG;
    if (arr->ndim != 3) return ZARR_ERR_UNSUPPORTED;
    if (axis < 0 || axis > 2) return ZARR_ERR_BOUNDS;
    if (start_idx < 0 || stop_idx > arr->shape[axis] || start_idx >= stop_idx)
        return ZARR_ERR_BOUNDS;

    // Output dimensions: the two non-projected axes
    int64_t dim0 = arr->shape[axis == 0 ? 1 : 0];
    int64_t dim1 = arr->shape[axis <= 1 ? 2 : 1];
    int64_t out_size = dim0 * dim1;
    if (bufsize < out_size) return ZARR_ERR_BOUNDS;

    // Initialize
    if (do_max) {
        memset(buf, 0, (size_t)out_size);
    } else {
        memset(buf, 0, (size_t)out_size);
    }

    // Accumulator for mean projection
    uint32_t* accum = nullptr;
    if (!do_max) {
        accum = ZARR_CALLOC((size_t)out_size, sizeof(uint32_t));
        if (!accum) return ZARR_ERR_ALLOC;
    }

    // Process one slice at a time along the projection axis
    int64_t slice_size = dim0 * dim1;
    uint8_t* slice = ZARR_MALLOC((size_t)slice_size);
    if (!slice) { ZARR_FREE(accum); return ZARR_ERR_ALLOC; }

    int64_t depth = stop_idx - start_idx;
    for (int64_t idx = start_idx; idx < stop_idx; idx++) {
        zarr_status s = zarr_slice(arr, axis, idx, slice, slice_size);
        if (s != ZARR_OK) { ZARR_FREE(slice); ZARR_FREE(accum); return s; }

        if (do_max) {
            for (int64_t i = 0; i < slice_size; i++) {
                if (slice[i] > buf[i]) buf[i] = slice[i];
            }
        } else {
            for (int64_t i = 0; i < slice_size; i++) {
                accum[i] += slice[i];
            }
        }
    }

    if (!do_max) {
        for (int64_t i = 0; i < out_size; i++) {
            buf[i] = (uint8_t)((accum[i] + depth / 2) / depth);
        }
        ZARR_FREE(accum);
    }

    ZARR_FREE(slice);
    return ZARR_OK;
}

zarr_status zarr_project_max(const zarr_array* arr, zarr_axis axis,
                             int64_t start_idx, int64_t stop_idx,
                             uint8_t* buf, int64_t bufsize) {
    return zarr__project(arr, axis, start_idx, stop_idx, buf, bufsize, true);
}

zarr_status zarr_project_mean(const zarr_array* arr, zarr_axis axis,
                              int64_t start_idx, int64_t stop_idx,
                              uint8_t* buf, int64_t bufsize) {
    return zarr__project(arr, axis, start_idx, stop_idx, buf, bufsize, false);
}

// ── Comparison ──────────────────────────────────────────────────────────────

zarr_status zarr_compare(const zarr_array* a, const int64_t* a_start,
                         const int64_t* a_stop, const zarr_array* b,
                         const int64_t* b_start, zarr_diff* diff) {
    if (!a || !a_start || !a_stop || !b || !b_start || !diff)
        return ZARR_ERR_NULL_ARG;
    if (a->ndim != b->ndim) return ZARR_ERR_BOUNDS;

    int64_t n = zarr__region_size(a->ndim, a_start, a_stop);
    if (n <= 0) return ZARR_ERR_BOUNDS;

    // Verify b region has same size
    int64_t b_stop[ZARR_MAX_DIM];
    for (int8_t i = 0; i < a->ndim; i++) {
        b_stop[i] = b_start[i] + (a_stop[i] - a_start[i]);
        if (b_stop[i] > b->shape[i]) return ZARR_ERR_BOUNDS;
    }

    uint8_t* abuf = ZARR_MALLOC((size_t)n);
    uint8_t* bbuf = ZARR_MALLOC((size_t)n);
    if (!abuf || !bbuf) { ZARR_FREE(abuf); ZARR_FREE(bbuf); return ZARR_ERR_ALLOC; }

    zarr_status s = zarr_read(a, a_start, a_stop, abuf, n);
    if (s != ZARR_OK) goto done;
    s = zarr_read(b, b_start, b_stop, bbuf, n);
    if (s != ZARR_OK) goto done;

    double sum_sq = 0, sum_abs = 0;
    uint8_t max_d = 0;
    int64_t diffs = 0;
    for (int64_t i = 0; i < n; i++) {
        int d = abs((int)abuf[i] - (int)bbuf[i]);
        sum_sq += (double)d * d;
        sum_abs += d;
        if ((uint8_t)d > max_d) max_d = (uint8_t)d;
        if (d > 0) diffs++;
    }

    diff->mse = sum_sq / (double)n;
    diff->mae = sum_abs / (double)n;
    diff->max_abs_err = max_d;
    diff->count = n;
    diff->diff_count = diffs;
    diff->psnr = (diff->mse > 0.0) ? 10.0 * log10(255.0 * 255.0 / diff->mse) : INFINITY;

done:
    ZARR_FREE(abuf);
    ZARR_FREE(bbuf);
    return s;
}

// ── Threshold / Bounding Box ────────────────────────────────────────────────

zarr_status zarr_count_above(const zarr_array* arr, const int64_t* start,
                             const int64_t* stop, uint8_t threshold,
                             int64_t* count) {
    if (!arr || !start || !stop || !count) return ZARR_ERR_NULL_ARG;

    int64_t n = zarr__region_size(arr->ndim, start, stop);
    if (n <= 0) return ZARR_ERR_BOUNDS;

    uint8_t* buf = ZARR_MALLOC((size_t)n);
    if (!buf) return ZARR_ERR_ALLOC;

    zarr_status s = zarr_read(arr, start, stop, buf, n);
    if (s != ZARR_OK) { ZARR_FREE(buf); return s; }

    int64_t c = 0;
    for (int64_t i = 0; i < n; i++) {
        if (buf[i] >= threshold) c++;
    }
    *count = c;
    ZARR_FREE(buf);
    return ZARR_OK;
}

zarr_status zarr_bounding_box(const zarr_array* arr, const int64_t* start,
                              const int64_t* stop, uint8_t threshold,
                              int64_t* bb_start, int64_t* bb_stop) {
    if (!arr || !start || !stop || !bb_start || !bb_stop)
        return ZARR_ERR_NULL_ARG;
    if (arr->ndim != 3) return ZARR_ERR_UNSUPPORTED;

    int64_t n = zarr__region_size(arr->ndim, start, stop);
    if (n <= 0) return ZARR_ERR_BOUNDS;

    uint8_t* buf = ZARR_MALLOC((size_t)n);
    if (!buf) return ZARR_ERR_ALLOC;

    zarr_status s = zarr_read(arr, start, stop, buf, n);
    if (s != ZARR_OK) { ZARR_FREE(buf); return s; }

    int64_t sz = stop[0] - start[0];
    int64_t sy = stop[1] - start[1];
    int64_t sx = stop[2] - start[2];

    int64_t min_z = sz, min_y = sy, min_x = sx;
    int64_t max_z = -1, max_y = -1, max_x = -1;

    for (int64_t z = 0; z < sz; z++) {
        for (int64_t y = 0; y < sy; y++) {
            for (int64_t x = 0; x < sx; x++) {
                if (buf[z * sy * sx + y * sx + x] >= threshold) {
                    if (z < min_z) min_z = z;
                    if (y < min_y) min_y = y;
                    if (x < min_x) min_x = x;
                    if (z > max_z) max_z = z;
                    if (y > max_y) max_y = y;
                    if (x > max_x) max_x = x;
                }
            }
        }
    }

    ZARR_FREE(buf);

    if (max_z < 0) return ZARR_ERR_BOUNDS;  // no voxels found

    bb_start[0] = start[0] + min_z;
    bb_start[1] = start[1] + min_y;
    bb_start[2] = start[2] + min_x;
    bb_stop[0] = start[0] + max_z + 1;
    bb_stop[1] = start[1] + max_y + 1;
    bb_stop[2] = start[2] + max_x + 1;
    return ZARR_OK;
}

// ── Apply LUT ───────────────────────────────────────────────────────────────

zarr_status zarr_apply_lut(zarr_array* arr, const int64_t* start,
                           const int64_t* stop, const uint8_t lut[256]) {
    if (!arr || !start || !stop || !lut) return ZARR_ERR_NULL_ARG;

    int64_t n = zarr__region_size(arr->ndim, start, stop);
    if (n <= 0) return ZARR_ERR_BOUNDS;

    uint8_t* buf = ZARR_MALLOC((size_t)n);
    if (!buf) return ZARR_ERR_ALLOC;

    zarr_status s = zarr_read(arr, start, stop, buf, n);
    if (s != ZARR_OK) { ZARR_FREE(buf); return s; }

    for (int64_t i = 0; i < n; i++) {
        buf[i] = lut[buf[i]];
    }

    s = zarr_write(arr, start, stop, buf, n);
    ZARR_FREE(buf);
    return s;
}

// ── LOD Pyramid ─────────────────────────────────────────────────────────────

zarr_status zarr_build_pyramid(const zarr_array* src, const char* base_path,
                               zarr_array** out_levels, int max_levels,
                               int* num_levels) {
    if (!src || !base_path || !out_levels || !num_levels)
        return ZARR_ERR_NULL_ARG;
    if (src->ndim != 3) return ZARR_ERR_UNSUPPORTED;
    if (max_levels < 1) return ZARR_ERR_BOUNDS;

    *num_levels = 0;

    // Level 0: copy of source (just open it, don't duplicate)
    // Actually, we need to create a copy at base_path/0
    char level_path[ZARR_PATH_MAX];
    snprintf(level_path, sizeof(level_path), "%s/0", base_path);
    zarr__mkdir_p(base_path);

    zarr_array* prev = nullptr;
    zarr_status s = zarr_create(&prev, level_path, src->ndim, src->shape);
    if (s != ZARR_OK) return s;

    // Copy data from src to level 0
    s = zarr_copy_region(src,
        (int64_t[]){0, 0, 0}, (int64_t[]){src->shape[0], src->shape[1], src->shape[2]},
        prev, (int64_t[]){0, 0, 0});
    if (s != ZARR_OK) { zarr_close(prev); return s; }

    out_levels[0] = prev;
    *num_levels = 1;

    // Generate successive levels
    for (int level = 1; level < max_levels; level++) {
        // Check if any dimension is 1 — stop
        bool too_small = false;
        for (int i = 0; i < 3; i++) {
            if (prev->shape[i] <= 1) { too_small = true; break; }
        }
        if (too_small) break;

        snprintf(level_path, sizeof(level_path), "%s/%d", base_path, level);

        zarr_array* next = nullptr;
        s = zarr_downsample_2x(prev, level_path, &next);
        if (s != ZARR_OK) break;

        out_levels[level] = next;
        *num_levels = level + 1;
        prev = next;
    }

    return ZARR_OK;
}

// ── Crop ────────────────────────────────────────────────────────────────────

zarr_status zarr_crop(const zarr_array* src, const int64_t* start,
                      const int64_t* stop, const char* dst_path,
                      zarr_array** out) {
    if (!src || !start || !stop || !dst_path || !out) return ZARR_ERR_NULL_ARG;

    int64_t crop_shape[ZARR_MAX_DIM];
    int64_t n = 1;
    for (int8_t i = 0; i < src->ndim; i++) {
        crop_shape[i] = stop[i] - start[i];
        if (crop_shape[i] <= 0 || stop[i] > src->shape[i] || start[i] < 0)
            return ZARR_ERR_BOUNDS;
        n *= crop_shape[i];
    }

    zarr_array* dst = nullptr;
    zarr_status s = zarr_create(&dst, dst_path, src->ndim, crop_shape);
    if (s != ZARR_OK) return s;

    uint8_t* buf = ZARR_MALLOC((size_t)n);
    if (!buf) { zarr_close(dst); return ZARR_ERR_ALLOC; }

    s = zarr_read(src, start, stop, buf, n);
    if (s != ZARR_OK) { ZARR_FREE(buf); zarr_close(dst); return s; }

    int64_t dst_start[ZARR_MAX_DIM] = {0};
    s = zarr_write(dst, dst_start, crop_shape, buf, n);
    ZARR_FREE(buf);
    if (s != ZARR_OK) { zarr_close(dst); return s; }

    *out = dst;
    return ZARR_OK;
}

// ── Window / Level ──────────────────────────────────────────────────────────

zarr_status zarr_window_level(zarr_array* arr, const int64_t* start,
                              const int64_t* stop,
                              uint8_t win_low, uint8_t win_high) {
    if (!arr || !start || !stop) return ZARR_ERR_NULL_ARG;
    if (win_low >= win_high) return ZARR_ERR_BOUNDS;

    uint8_t lut[256];
    float range = (float)(win_high - win_low);
    for (int i = 0; i < 256; i++) {
        if (i <= win_low) lut[i] = 0;
        else if (i >= win_high) lut[i] = 255;
        else lut[i] = (uint8_t)(((float)(i - win_low) / range) * 255.0f + 0.5f);
    }
    return zarr_apply_lut(arr, start, stop, lut);
}

// ── 3D Box Blur ─────────────────────────────────────────────────────────────

zarr_status zarr_box_blur(const zarr_array* src, const int64_t* start,
                          const int64_t* stop, int radius,
                          zarr_array* dst, const int64_t* dst_start) {
    if (!src || !start || !stop || !dst || !dst_start) return ZARR_ERR_NULL_ARG;
    if (src->ndim != 3 || dst->ndim != 3) return ZARR_ERR_UNSUPPORTED;
    if (radius < 1) return ZARR_ERR_BOUNDS;

    int64_t sz = stop[0] - start[0];
    int64_t sy = stop[1] - start[1];
    int64_t sx = stop[2] - start[2];
    int64_t n = sz * sy * sx;
    if (n <= 0) return ZARR_ERR_BOUNDS;

    uint8_t* in = ZARR_MALLOC((size_t)n);
    uint8_t* out_buf = ZARR_MALLOC((size_t)n);
    if (!in || !out_buf) { ZARR_FREE(in); ZARR_FREE(out_buf); return ZARR_ERR_ALLOC; }

    zarr_status s = zarr_read(src, start, stop, in, n);
    if (s != ZARR_OK) { ZARR_FREE(in); ZARR_FREE(out_buf); return s; }

    int r = radius;
    for (int64_t z = 0; z < sz; z++) {
        for (int64_t y = 0; y < sy; y++) {
            for (int64_t x = 0; x < sx; x++) {
                uint32_t sum = 0;
                int count = 0;
                for (int dz = -r; dz <= r; dz++) {
                    int64_t zz = z + dz;
                    if (zz < 0 || zz >= sz) continue;
                    for (int dy = -r; dy <= r; dy++) {
                        int64_t yy = y + dy;
                        if (yy < 0 || yy >= sy) continue;
                        for (int dx = -r; dx <= r; dx++) {
                            int64_t xx = x + dx;
                            if (xx < 0 || xx >= sx) continue;
                            sum += in[zz * sy * sx + yy * sx + xx];
                            count++;
                        }
                    }
                }
                out_buf[z * sy * sx + y * sx + x] = (uint8_t)((sum + count / 2) / count);
            }
        }
    }

    int64_t dst_stop_r[3] = {
        dst_start[0] + sz, dst_start[1] + sy, dst_start[2] + sx
    };
    s = zarr_write(dst, dst_start, dst_stop_r, out_buf, n);
    ZARR_FREE(in);
    ZARR_FREE(out_buf);
    return s;
}

// ── Gradient Magnitude (Sobel) ──────────────────────────────────────────────

zarr_status zarr_gradient_magnitude(const zarr_array* src,
                                    const int64_t* start, const int64_t* stop,
                                    zarr_array* dst, const int64_t* dst_start) {
    if (!src || !start || !stop || !dst || !dst_start) return ZARR_ERR_NULL_ARG;
    if (src->ndim != 3 || dst->ndim != 3) return ZARR_ERR_UNSUPPORTED;

    int64_t sz = stop[0] - start[0];
    int64_t sy = stop[1] - start[1];
    int64_t sx = stop[2] - start[2];
    int64_t n = sz * sy * sx;
    if (n <= 0 || sz < 3 || sy < 3 || sx < 3) return ZARR_ERR_BOUNDS;

    uint8_t* in = ZARR_MALLOC((size_t)n);
    uint8_t* out_buf = ZARR_CALLOC((size_t)n, 1);
    if (!in || !out_buf) { ZARR_FREE(in); ZARR_FREE(out_buf); return ZARR_ERR_ALLOC; }

    zarr_status s = zarr_read(src, start, stop, in, n);
    if (s != ZARR_OK) { ZARR_FREE(in); ZARR_FREE(out_buf); return s; }

    #define V(z,y,x) ((float)in[(z)*sy*sx + (y)*sx + (x)])

    for (int64_t z = 1; z < sz - 1; z++) {
        for (int64_t y = 1; y < sy - 1; y++) {
            for (int64_t x = 1; x < sx - 1; x++) {
                // Central differences for gradient
                float gx = (V(z,y,x+1) - V(z,y,x-1)) * 0.5f;
                float gy = (V(z,y+1,x) - V(z,y-1,x)) * 0.5f;
                float gz = (V(z+1,y,x) - V(z-1,y,x)) * 0.5f;
                float mag = sqrtf(gx*gx + gy*gy + gz*gz);
                int v = (int)(mag + 0.5f);
                out_buf[z * sy * sx + y * sx + x] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
    }
    #undef V

    int64_t dst_stop[3] = {
        dst_start[0] + sz, dst_start[1] + sy, dst_start[2] + sx
    };
    s = zarr_write(dst, dst_start, dst_stop, out_buf, n);
    ZARR_FREE(in);
    ZARR_FREE(out_buf);
    return s;
}

// ── Min Projection ──────────────────────────────────────────────────────────

zarr_status zarr_project_min(const zarr_array* arr, zarr_axis axis,
                             int64_t start_idx, int64_t stop_idx,
                             uint8_t* buf, int64_t bufsize) {
    if (!arr || !buf) return ZARR_ERR_NULL_ARG;
    if (arr->ndim != 3) return ZARR_ERR_UNSUPPORTED;
    if (axis < 0 || axis > 2) return ZARR_ERR_BOUNDS;
    if (start_idx < 0 || stop_idx > arr->shape[axis] || start_idx >= stop_idx)
        return ZARR_ERR_BOUNDS;

    int64_t dim0 = arr->shape[axis == 0 ? 1 : 0];
    int64_t dim1 = arr->shape[axis <= 1 ? 2 : 1];
    int64_t out_size = dim0 * dim1;
    if (bufsize < out_size) return ZARR_ERR_BOUNDS;

    memset(buf, 255, (size_t)out_size);

    uint8_t* slice = ZARR_MALLOC((size_t)out_size);
    if (!slice) return ZARR_ERR_ALLOC;

    for (int64_t idx = start_idx; idx < stop_idx; idx++) {
        zarr_status s = zarr_slice(arr, axis, idx, slice, out_size);
        if (s != ZARR_OK) { ZARR_FREE(slice); return s; }
        for (int64_t i = 0; i < out_size; i++) {
            if (slice[i] < buf[i]) buf[i] = slice[i];
        }
    }

    ZARR_FREE(slice);
    return ZARR_OK;
}

// ── Occupancy ───────────────────────────────────────────────────────────────

zarr_status zarr_occupancy(const zarr_array* arr, const int64_t* start,
                           const int64_t* stop, double* out) {
    if (!arr || !start || !stop || !out) return ZARR_ERR_NULL_ARG;

    int64_t n = zarr__region_size(arr->ndim, start, stop);
    if (n <= 0) return ZARR_ERR_BOUNDS;

    uint8_t* buf = ZARR_MALLOC((size_t)n);
    if (!buf) return ZARR_ERR_ALLOC;

    zarr_status s = zarr_read(arr, start, stop, buf, n);
    if (s != ZARR_OK) { ZARR_FREE(buf); return s; }

    int64_t nonzero = 0;
    for (int64_t i = 0; i < n; i++) {
        if (buf[i] != 0) nonzero++;
    }
    *out = (double)nonzero / (double)n;
    ZARR_FREE(buf);
    return ZARR_OK;
}

// ── Zarr v3 Metadata Writer ─────────────────────────────────────────────────

zarr_status zarr_write_v3_metadata(const zarr_array* arr, const char* path) {
    if (!arr || !path) return ZARR_ERR_NULL_ARG;

    char buf[4096];
    int pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "{\n"
        "  \"zarr_format\": 3,\n"
        "  \"node_type\": \"array\",\n"
        "  \"shape\": [");
    for (int8_t i = 0; i < arr->ndim; i++) {
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ", ");
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%lld", (long long)arr->shape[i]);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "],\n"
        "  \"data_type\": \"uint8\",\n"
        "  \"chunk_grid\": {\n"
        "    \"name\": \"regular\",\n"
        "    \"configuration\": {\n"
        "      \"chunk_shape\": [");
    for (int8_t i = 0; i < arr->ndim; i++) {
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ", ");
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", ZARR_SHARD_DIM);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "]\n"
        "    }\n"
        "  },\n"
        "  \"chunk_key_encoding\": {\n"
        "    \"name\": \"default\",\n"
        "    \"configuration\": { \"separator\": \"/\" }\n"
        "  },\n"
        "  \"fill_value\": 0,\n"
        "  \"codecs\": [\n"
        "    {\n"
        "      \"name\": \"sharding_indexed\",\n"
        "      \"configuration\": {\n"
        "        \"chunk_shape\": [");
    for (int8_t i = 0; i < arr->ndim; i++) {
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ", ");
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", ZARR_CHUNK_DIM);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "],\n"
        "        \"codecs\": [\n"
        "          { \"name\": \"bytes\", \"configuration\": { \"endian\": \"little\" } },\n"
        "          { \"name\": \"vl264\", \"configuration\": {} }\n"
        "        ],\n"
        "        \"index_codecs\": [\n"
        "          { \"name\": \"bytes\", \"configuration\": { \"endian\": \"little\" } }\n"
        "        ],\n"
        "        \"index_location\": \"end\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");

    char full_path[ZARR_PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/zarr.json", path);
    if (!zarr__write_file(full_path, buf, (size_t)pos)) return ZARR_ERR_IO;
    return ZARR_OK;
}

// ── Utilities ───────────────────────────────────────────────────────────────

const char* zarr_status_str(zarr_status s) {
    switch (s) {
        case ZARR_OK:               return "ok";
        case ZARR_ERR_NULL_ARG:     return "null argument";
        case ZARR_ERR_ALLOC:        return "allocation failure";
        case ZARR_ERR_IO:           return "i/o error";
        case ZARR_ERR_FORMAT:       return "invalid zarr format";
        case ZARR_ERR_DTYPE:        return "unsupported data type";
        case ZARR_ERR_CODEC:        return "codec error";
        case ZARR_ERR_BOUNDS:       return "index out of bounds";
        case ZARR_ERR_UNSUPPORTED:  return "unsupported feature";
        case ZARR_ERR_BLOSC:        return "blosc2 error";
        case ZARR_ERR_VL264:        return "vl264 error";
    }
    return "unknown error";
}

const char* zarr_version_str(void) {
    return "0.1.0";
}

// ── Groups & Hierarchy ──────────────────────────────────────────────────────

struct zarr_group {
    char    path[ZARR_PATH_MAX];
    int     version;        // 2 or 3
    char*   attrs_json;     // raw .zattrs / attributes JSON (nullptr if none)
    size_t  attrs_len;
};

zarr_status zarr_group_open(zarr_group** out, const char* path) {
    if (!out || !path) return ZARR_ERR_NULL_ARG;

    zarr_group* grp = ZARR_CALLOC(1, sizeof(zarr_group));
    if (!grp) return ZARR_ERR_ALLOC;
    snprintf(grp->path, ZARR_PATH_MAX, "%s", path);

    // Try v3 first: zarr.json with node_type=group
    char meta_path[ZARR_PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s/zarr.json", path);
    if (zarr__file_exists(meta_path)) {
        size_t mlen;
        char* mjson = zarr__read_file(meta_path, &mlen);
        if (mjson) {
            zarr__json j = { .data = mjson, .pos = 0, .len = mlen };
            if (zarr__json_find_key(&j, "node_type")) {
                char nt[32];
                zarr__json_string(&j, nt, sizeof(nt));
                if (strcmp(nt, "group") == 0) {
                    grp->version = 3;
                    // Extract attributes
                    if (zarr__json_find_key(&j, "attributes")) {
                        size_t attr_start = j.pos;
                        zarr__json_skip_value(&j);
                        size_t attr_len = j.pos - attr_start;
                        grp->attrs_json = ZARR_MALLOC(attr_len + 1);
                        if (grp->attrs_json) {
                            memcpy(grp->attrs_json, mjson + attr_start, attr_len);
                            grp->attrs_json[attr_len] = '\0';
                            grp->attrs_len = attr_len;
                        }
                    }
                    ZARR_FREE(mjson);
                    *out = grp;
                    return ZARR_OK;
                }
            }
            ZARR_FREE(mjson);
        }
    }

    // Try v2: .zgroup
    snprintf(meta_path, sizeof(meta_path), "%s/.zgroup", path);
    if (zarr__file_exists(meta_path)) {
        grp->version = 2;
        // Read .zattrs if present
        snprintf(meta_path, sizeof(meta_path), "%s/.zattrs", path);
        if (zarr__file_exists(meta_path)) {
            grp->attrs_json = zarr__read_file(meta_path, &grp->attrs_len);
        }
        *out = grp;
        return ZARR_OK;
    }

    // Neither found — check if it's a directory at all
    if (zarr__is_dir(path)) {
        // Treat as implicit group (common in practice)
        grp->version = 2;
        snprintf(meta_path, sizeof(meta_path), "%s/.zattrs", path);
        if (zarr__file_exists(meta_path)) {
            grp->attrs_json = zarr__read_file(meta_path, &grp->attrs_len);
        }
        *out = grp;
        return ZARR_OK;
    }

    ZARR_FREE(grp);
    return ZARR_ERR_FORMAT;
}

void zarr_group_close(zarr_group* grp) {
    if (!grp) return;
    ZARR_FREE(grp->attrs_json);
    ZARR_FREE(grp);
}

int zarr_group_version(const zarr_group* grp) {
    return grp ? grp->version : 0;
}

const char* zarr_group_path(const zarr_group* grp) {
    return grp ? grp->path : nullptr;
}

zarr_status zarr_group_list(const zarr_group* grp, char*** names, int* count) {
    if (!grp || !names || !count) return ZARR_ERR_NULL_ARG;

    DIR* dir = opendir(grp->path);
    if (!dir) return ZARR_ERR_IO;

    // First pass: count entries
    int n = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;  // skip hidden + . + ..
        // Skip blosc2 internal files
        if (strcmp(ent->d_name, "chunks.b2frame") == 0) continue;
        // Check if it's a directory (subgroup or array)
        char child_path[ZARR_PATH_MAX];
        snprintf(child_path, sizeof(child_path), "%s/%s", grp->path, ent->d_name);
        if (zarr__is_dir(child_path)) n++;
    }

    char** result = ZARR_CALLOC((size_t)n, sizeof(char*));
    if (!result) { closedir(dir); return ZARR_ERR_ALLOC; }

    rewinddir(dir);
    int idx = 0;
    while ((ent = readdir(dir)) != nullptr && idx < n) {
        if (ent->d_name[0] == '.') continue;
        if (strcmp(ent->d_name, "chunks.b2frame") == 0) continue;
        char child_path[ZARR_PATH_MAX];
        snprintf(child_path, sizeof(child_path), "%s/%s", grp->path, ent->d_name);
        if (zarr__is_dir(child_path)) {
            result[idx] = ZARR_MALLOC(strlen(ent->d_name) + 1);
            if (result[idx]) {
                strcpy(result[idx], ent->d_name);
                idx++;
            }
        }
    }
    closedir(dir);

    *names = result;
    *count = idx;
    return ZARR_OK;
}

bool zarr_group_is_array(const zarr_group* grp, const char* name) {
    if (!grp || !name) return false;
    char child_path[ZARR_PATH_MAX];
    // v2: has .zarray
    snprintf(child_path, sizeof(child_path), "%s/%s/.zarray", grp->path, name);
    if (zarr__file_exists(child_path)) return true;
    // v3: has zarr.json with node_type=array
    snprintf(child_path, sizeof(child_path), "%s/%s/zarr.json", grp->path, name);
    if (zarr__file_exists(child_path)) {
        size_t mlen;
        char* mjson = zarr__read_file(child_path, &mlen);
        if (mjson) {
            bool is_arr = strstr(mjson, "\"node_type\": \"array\"") != nullptr
                       || strstr(mjson, "\"node_type\":\"array\"") != nullptr;
            ZARR_FREE(mjson);
            return is_arr;
        }
    }
    // b2nd sparse frame directory (our canonical format)
    snprintf(child_path, sizeof(child_path), "%s/%s/chunks.b2frame", grp->path, name);
    if (zarr__file_exists(child_path)) return true;
    return false;
}

zarr_status zarr_group_open_array(const zarr_group* grp, const char* name,
                                  zarr_array** out) {
    if (!grp || !name || !out) return ZARR_ERR_NULL_ARG;
    char child_path[ZARR_PATH_MAX];
    snprintf(child_path, sizeof(child_path), "%s/%s", grp->path, name);
    return zarr_open(out, child_path);
}

// ── Attributes ──────────────────────────────────────────────────────────────

static const char* zarr__attr_find(const zarr_group* grp, const char* key) {
    if (!grp || !grp->attrs_json || !key) return nullptr;
    zarr__json j = { .data = grp->attrs_json, .pos = 0, .len = grp->attrs_len };
    if (zarr__json_find_key(&j, key)) {
        return grp->attrs_json + j.pos;
    }
    return nullptr;
}

char* zarr_attr_get_string(const zarr_group* grp, const char* key) {
    const char* pos = zarr__attr_find(grp, key);
    if (!pos) return nullptr;
    zarr__json j = { .data = pos, .pos = 0, .len = strlen(pos) };
    char buf[1024];
    if (zarr__json_string(&j, buf, sizeof(buf))) {
        char* result = ZARR_MALLOC(strlen(buf) + 1);
        if (result) strcpy(result, buf);
        return result;
    }
    return nullptr;
}

int64_t zarr_attr_get_int(const zarr_group* grp, const char* key, int64_t default_val) {
    const char* pos = zarr__attr_find(grp, key);
    if (!pos) return default_val;
    zarr__json j = { .data = pos, .pos = 0, .len = strlen(pos) };
    int64_t val;
    if (zarr__json_int(&j, &val)) return val;
    return default_val;
}

double zarr_attr_get_float(const zarr_group* grp, const char* key, double default_val) {
    const char* pos = zarr__attr_find(grp, key);
    if (!pos) return default_val;
    zarr__json j = { .data = pos, .pos = 0, .len = strlen(pos) };
    double val;
    if (zarr__json_float(&j, &val)) return val;
    return default_val;
}

// ── Consolidated Metadata ───────────────────────────────────────────────────

zarr_status zarr_group_open_consolidated(zarr_group** out, const char* path) {
    if (!out || !path) return ZARR_ERR_NULL_ARG;
    // For now, consolidated metadata is used only to verify the store exists.
    // The actual metadata is still read per-array/group.
    // Check for .zmetadata (v2) or zarr.json (v3) at root.
    char meta_path[ZARR_PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s/.zmetadata", path);
    if (zarr__file_exists(meta_path)) {
        return zarr_group_open(out, path);
    }
    snprintf(meta_path, sizeof(meta_path), "%s/zarr.json", path);
    if (zarr__file_exists(meta_path)) {
        return zarr_group_open(out, path);
    }
    // Fall back to regular open
    return zarr_group_open(out, path);
}
