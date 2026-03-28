// tensor.h — Pure C23 n-dimensional tensor library
// Single-header: declare API, then #define TS_IMPLEMENTATION in one .c file.
// Vesuvius Challenge / Villa Volume Cartographer.
#ifndef TENSOR_H
#define TENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ── Version ─────────────────────────────────────────────────────────────────

#define TS_VERSION_MAJOR 0
#define TS_VERSION_MINOR 1
#define TS_VERSION_PATCH 0

// ── C23 Compat ──────────────────────────────────────────────────────────────

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define TS_NODISCARD    [[nodiscard]]
  #define TS_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define TS_NODISCARD    __attribute__((warn_unused_result))
  #define TS_MAYBE_UNUSED __attribute__((unused))
#else
  #define TS_NODISCARD
  #define TS_MAYBE_UNUSED
#endif

// ── Linkage ─────────────────────────────────────────────────────────────────

#ifndef TSDEF
  #ifdef TS_STATIC
    #define TSDEF static
  #else
    #define TSDEF extern
  #endif
#endif

// ── Allocator Hooks ─────────────────────────────────────────────────────────

#ifndef TS_MALLOC
  #include <stdlib.h>
  #define TS_MALLOC(sz)       malloc(sz)
  #define TS_REALLOC(p, sz)   realloc(p, sz)
  #define TS_FREE(p)          free(p)
  #define TS_CALLOC(n, sz)    calloc(n, sz)
#endif

// ── Constants ───────────────────────────────────────────────────────────────

#define TS_MAX_DIM 8

// ── Status Codes ────────────────────────────────────────────────────────────

typedef enum ts_status {
    TS_OK = 0,
    TS_ERR_NULL_ARG,
    TS_ERR_ALLOC,
    TS_ERR_SHAPE,         // shape mismatch or invalid dimensions
    TS_ERR_DTYPE,         // unsupported or mismatched dtype
    TS_ERR_BOUNDS,        // index out of range
    TS_ERR_STRIDE,        // non-contiguous where contiguous required
    TS_ERR_IO,            // storage backend error
    TS_ERR_UNSUPPORTED,   // unimplemented operation
} ts_status;

// ── Data Types ──────────────────────────────────────────────────────────────

typedef enum ts_dtype {
    TS_U8 = 0, TS_U16, TS_U32, TS_U64,
    TS_I8, TS_I16, TS_I32, TS_I64,
    TS_F32, TS_F64,
    TS_DTYPE_COUNT,
} ts_dtype;

// ── Flags ───────────────────────────────────────────────────────────────────

#define TS_FLAG_OWNS_DATA   (1u << 0)
#define TS_FLAG_CONTIGUOUS  (1u << 1)

// ── Tensor ──────────────────────────────────────────────────────────────────

typedef struct ts_tensor {
    void*    data;                  // pointer to first element
    int64_t  shape[TS_MAX_DIM];     // size along each axis
    int64_t  strides[TS_MAX_DIM];   // byte strides along each axis
    int8_t   ndim;                  // number of dimensions (1..8)
    ts_dtype dtype;                 // element type
    uint32_t flags;                 // TS_FLAG_OWNS_DATA | TS_FLAG_CONTIGUOUS
} ts_tensor;

// ── Slice ───────────────────────────────────────────────────────────────────

typedef struct ts_slice {
    int64_t start;    // inclusive (negative = from end)
    int64_t stop;     // exclusive (negative = from end, INT64_MAX = to end)
    int64_t step;     // stride multiplier (must be != 0)
} ts_slice;

#define TS_ALL ((ts_slice){0, INT64_MAX, 1})

// ── Dtype Helpers ───────────────────────────────────────────────────────────

TSDEF int64_t     ts_dtype_size(ts_dtype d);
TSDEF const char* ts_dtype_str(ts_dtype d);

// ── Lifecycle ───────────────────────────────────────────────────────────────

TS_NODISCARD TSDEF ts_status ts_empty(ts_tensor** out, int8_t ndim,
                                      const int64_t* shape, ts_dtype dtype);
TS_NODISCARD TSDEF ts_status ts_zeros(ts_tensor** out, int8_t ndim,
                                      const int64_t* shape, ts_dtype dtype);
TS_NODISCARD TSDEF ts_status ts_ones(ts_tensor** out, int8_t ndim,
                                     const int64_t* shape, ts_dtype dtype);
TS_NODISCARD TSDEF ts_status ts_full(ts_tensor** out, int8_t ndim,
                                     const int64_t* shape, ts_dtype dtype,
                                     const void* fill_value);

// Wrap existing buffer (tensor does NOT own the data).
TS_NODISCARD TSDEF ts_status ts_from_data(ts_tensor** out, void* data,
                                          int8_t ndim, const int64_t* shape,
                                          ts_dtype dtype);

// Deep copy.
TS_NODISCARD TSDEF ts_status ts_clone(ts_tensor** out, const ts_tensor* src);

// Free tensor (frees data only if TS_FLAG_OWNS_DATA is set).
TSDEF void ts_free(ts_tensor* t);

// ── Element Access ──────────────────────────────────────────────────────────

TSDEF void*    ts_ptr(const ts_tensor* t, const int64_t* indices);

TSDEF float    ts_get_f32(const ts_tensor* t, const int64_t* indices);
TSDEF double   ts_get_f64(const ts_tensor* t, const int64_t* indices);
TSDEF uint8_t  ts_get_u8(const ts_tensor* t, const int64_t* indices);
TSDEF uint16_t ts_get_u16(const ts_tensor* t, const int64_t* indices);
TSDEF int32_t  ts_get_i32(const ts_tensor* t, const int64_t* indices);
TSDEF int64_t  ts_get_i64(const ts_tensor* t, const int64_t* indices);

TSDEF void ts_set_f32(ts_tensor* t, const int64_t* indices, float val);
TSDEF void ts_set_f64(ts_tensor* t, const int64_t* indices, double val);
TSDEF void ts_set_u8(ts_tensor* t, const int64_t* indices, uint8_t val);
TSDEF void ts_set_u16(ts_tensor* t, const int64_t* indices, uint16_t val);
TSDEF void ts_set_i32(ts_tensor* t, const int64_t* indices, int32_t val);
TSDEF void ts_set_i64(ts_tensor* t, const int64_t* indices, int64_t val);

// ── Views & Slicing (zero-copy) ────────────────────────────────────────────

// Slice along each dimension. slices array must have src->ndim elements.
TS_NODISCARD TSDEF ts_status ts_view(ts_tensor** out, const ts_tensor* src,
                                     const ts_slice* slices);

// Reshape (must be contiguous, total elements must match).
TS_NODISCARD TSDEF ts_status ts_reshape(ts_tensor** out, const ts_tensor* src,
                                        int8_t ndim, const int64_t* new_shape);

// Transpose axes. axes=NULL reverses all dimensions.
TS_NODISCARD TSDEF ts_status ts_transpose(ts_tensor** out, const ts_tensor* src,
                                          const int8_t* axes);

// Remove a size-1 dimension.
TS_NODISCARD TSDEF ts_status ts_squeeze(ts_tensor** out, const ts_tensor* src,
                                        int8_t axis);

// Insert a size-1 dimension.
TS_NODISCARD TSDEF ts_status ts_unsqueeze(ts_tensor** out, const ts_tensor* src,
                                          int8_t axis);

// ── Elementwise Ops ─────────────────────────────────────────────────────────

// Binary ops with NumPy-style broadcasting.
TS_NODISCARD TSDEF ts_status ts_add(ts_tensor** out, const ts_tensor* a,
                                    const ts_tensor* b);
TS_NODISCARD TSDEF ts_status ts_sub(ts_tensor** out, const ts_tensor* a,
                                    const ts_tensor* b);
TS_NODISCARD TSDEF ts_status ts_mul(ts_tensor** out, const ts_tensor* a,
                                    const ts_tensor* b);
TS_NODISCARD TSDEF ts_status ts_div(ts_tensor** out, const ts_tensor* a,
                                    const ts_tensor* b);

// Scalar multiply.
TS_NODISCARD TSDEF ts_status ts_scale(ts_tensor** out, const ts_tensor* src,
                                      double scalar);

// Type cast.
TS_NODISCARD TSDEF ts_status ts_cast(ts_tensor** out, const ts_tensor* src,
                                     ts_dtype target);

// ── Reductions ──────────────────────────────────────────────────────────────

// axis = -1 reduces over all dimensions (returns scalar tensor).
TS_NODISCARD TSDEF ts_status ts_sum(ts_tensor** out, const ts_tensor* src,
                                    int8_t axis);
TS_NODISCARD TSDEF ts_status ts_mean(ts_tensor** out, const ts_tensor* src,
                                     int8_t axis);
TS_NODISCARD TSDEF ts_status ts_min(ts_tensor** out, const ts_tensor* src,
                                    int8_t axis);
TS_NODISCARD TSDEF ts_status ts_max(ts_tensor** out, const ts_tensor* src,
                                    int8_t axis);

// ── Unary Math Ops ──────────────────────────────────────────────────────────

TS_NODISCARD TSDEF ts_status ts_abs(ts_tensor** out, const ts_tensor* src);
TS_NODISCARD TSDEF ts_status ts_neg(ts_tensor** out, const ts_tensor* src);
TS_NODISCARD TSDEF ts_status ts_sqrt(ts_tensor** out, const ts_tensor* src);
TS_NODISCARD TSDEF ts_status ts_exp(ts_tensor** out, const ts_tensor* src);
TS_NODISCARD TSDEF ts_status ts_log(ts_tensor** out, const ts_tensor* src);

// Clamp all elements to [lo, hi].
TS_NODISCARD TSDEF ts_status ts_clamp(ts_tensor** out, const ts_tensor* src,
                                      double lo, double hi);

// ── Comparison (element-wise, output TS_U8 with 0/1) ───────────────────────

TS_NODISCARD TSDEF ts_status ts_eq(ts_tensor** out, const ts_tensor* a,
                                   const ts_tensor* b);
TS_NODISCARD TSDEF ts_status ts_gt(ts_tensor** out, const ts_tensor* a,
                                   const ts_tensor* b);
TS_NODISCARD TSDEF ts_status ts_lt(ts_tensor** out, const ts_tensor* a,
                                   const ts_tensor* b);

// ── Where (ternary select) ─────────────────────────────────────────────────

// out[i] = cond[i] ? a[i] : b[i].  cond must be TS_U8.
TS_NODISCARD TSDEF ts_status ts_where(ts_tensor** out, const ts_tensor* cond,
                                      const ts_tensor* a, const ts_tensor* b);

// ── Matmul ──────────────────────────────────────────────────────────────────

// 2D matrix multiply: a [M,K] x b [K,N] -> out [M,N].
// Supports any numeric dtype; output dtype promoted like elementwise ops.
TS_NODISCARD TSDEF ts_status ts_matmul(ts_tensor** out, const ts_tensor* a,
                                       const ts_tensor* b);

// ── Interpolation (3D volume sampling) ─────────────────────────────────────

typedef enum ts_interp {
    TS_NEAREST = 0,
    TS_TRILINEAR,
    TS_TRICUBIC,
} ts_interp;

// Sample a 3D tensor at fractional coordinates.
// coords: [N, 3] tensor of (z, y, x) float coordinates.
// out: [N] tensor of interpolated values (dtype matches vol or F32/F64).
TS_NODISCARD TSDEF ts_status ts_sample3d(ts_tensor** out, const ts_tensor* vol,
                                         const ts_tensor* coords,
                                         ts_interp method);

// Sample a 3D tensor at a single point. Returns the value as double.
TSDEF double ts_sample3d_at(const ts_tensor* vol,
                            double z, double y, double x,
                            ts_interp method);

// ── Downsample (pyramid building) ──────────────────────────────────────────

// 2x downsample a 3D tensor by averaging 2x2x2 blocks.
// Input shape must be even along all 3 dims. Output shape = input/2.
TS_NODISCARD TSDEF ts_status ts_downsample3d_2x(ts_tensor** out,
                                                const ts_tensor* src);

// ── Copy / Paste (region transfer) ─────────────────────────────────────────

// Copy src into dst at the given offset. Shapes must be compatible.
// dst[offset:offset+src.shape] = src
TSDEF ts_status ts_paste(ts_tensor* dst, const ts_tensor* src,
                         const int64_t* offset);

// ── Map (apply function to each element) ───────────────────────────────────

typedef double (*ts_map_fn)(double val, void* userdata);

// Apply fn to every element, writing result to a new tensor.
TS_NODISCARD TSDEF ts_status ts_map(ts_tensor** out, const ts_tensor* src,
                                    ts_map_fn fn, void* userdata);

// Apply fn in-place to every element.
TSDEF ts_status ts_map_inplace(ts_tensor* t, ts_map_fn fn, void* userdata);

// ── Virtual Tensor ─────────────────────────────────────────────────────────
// A tensor that represents data without owning or caching it.
// Fetches data on demand via user-provided callbacks.
// libtensor does representation + computation; the caller handles storage,
// caching, compression, transport (zarr, mlcache, libs3, vl264, etc).

typedef struct ts_virtual ts_virtual;

// Callback: read a single voxel. Must handle bounds (return 0 for OOB).
typedef double (*ts_voxel_fn)(const int64_t* coords, void* userdata);

// Callback: bulk-read a region [start, stop) into a contiguous buffer.
// buf is pre-allocated with the right number of elements * dtype_size bytes.
// Optional — if NULL, libtensor falls back to per-voxel reads.
typedef ts_status (*ts_region_fn)(const int64_t* start, const int64_t* stop,
                                  void* buf, void* userdata);

// Create a virtual tensor. ndim can be 1..8 (not limited to 3D).
TS_NODISCARD TSDEF ts_status ts_virtual_create(ts_virtual** out,
                                               int8_t ndim,
                                               const int64_t* shape,
                                               ts_dtype dtype,
                                               ts_voxel_fn voxel_fn,
                                               ts_region_fn region_fn,
                                               void* userdata);

TSDEF void ts_virtual_free(ts_virtual* vt);

// Single-element access.
TSDEF double ts_virtual_get(ts_virtual* vt, const int64_t* coords);

// Convenience for 3D: ts_virtual_get3(vt, z, y, x).
TSDEF double ts_virtual_get3(ts_virtual* vt, int64_t z, int64_t y, int64_t x);

// Materialize a region [start, stop) into a dense ts_tensor.
// Uses region_fn if available, otherwise falls back to voxel_fn.
TS_NODISCARD TSDEF ts_status ts_virtual_read(ts_virtual* vt, ts_tensor** out,
                                             const int64_t* start,
                                             const int64_t* stop);

// Interpolated 3D sampling (virtual tensor must be 3D).
TSDEF double ts_virtual_sample(ts_virtual* vt, double z, double y, double x,
                               ts_interp method);

// Batch interpolated 3D sampling.
// coords: [N, 3] tensor of (z, y, x) float coordinates.
TS_NODISCARD TSDEF ts_status ts_virtual_sample3d(ts_virtual* vt,
                                                 ts_tensor** out,
                                                 const ts_tensor* coords,
                                                 ts_interp method);

// Query metadata.
TSDEF int8_t         ts_virtual_ndim(const ts_virtual* vt);
TSDEF const int64_t* ts_virtual_shape(const ts_virtual* vt);
TSDEF ts_dtype       ts_virtual_dtype(const ts_virtual* vt);

// ── In-place Binary Ops ────────────────────────────────────────────────────

// a += b, a -= b, etc. with broadcasting. a is modified in-place.
TSDEF ts_status ts_add_inplace(ts_tensor* a, const ts_tensor* b);
TSDEF ts_status ts_sub_inplace(ts_tensor* a, const ts_tensor* b);
TSDEF ts_status ts_mul_inplace(ts_tensor* a, const ts_tensor* b);
TSDEF ts_status ts_div_inplace(ts_tensor* a, const ts_tensor* b);
TSDEF ts_status ts_scale_inplace(ts_tensor* a, double scalar);

// ── Linear Algebra ─────────────────────────────────────────────────────────

// Inner (dot) product of two 1D tensors. Result is a scalar tensor [1].
TS_NODISCARD TSDEF ts_status ts_dot(ts_tensor** out, const ts_tensor* a,
                                    const ts_tensor* b);

// L2 norm of a tensor (all elements). Returns scalar tensor [1] as F64.
TS_NODISCARD TSDEF ts_status ts_norm(ts_tensor** out, const ts_tensor* src);

// ── Argmin / Argmax ─────────────────────────────────────────────────────────

// Global argmin/argmax — returns index as int64_t (flat index into contiguous layout).
TSDEF int64_t ts_argmin(const ts_tensor* t);
TSDEF int64_t ts_argmax(const ts_tensor* t);

// ── Concatenate / Stack ─────────────────────────────────────────────────────

// Concatenate N tensors along an existing axis.
// All tensors must match in all dims except the concat axis.
TS_NODISCARD TSDEF ts_status ts_cat(ts_tensor** out, const ts_tensor** tensors,
                                    int n, int8_t axis);

// Stack N tensors along a new axis (inserts a dimension).
// All tensors must have identical shapes.
TS_NODISCARD TSDEF ts_status ts_stack(ts_tensor** out, const ts_tensor** tensors,
                                      int n, int8_t axis);

// ── Axis Manipulation (zero-copy) ──────────────────────────────────────────

// Reverse elements along an axis (negative stride trick).
TS_NODISCARD TSDEF ts_status ts_flip(ts_tensor** out, const ts_tensor* src,
                                     int8_t axis);

// Repeat a tensor along an axis by copying data.
TS_NODISCARD TSDEF ts_status ts_repeat(ts_tensor** out, const ts_tensor* src,
                                       int8_t axis, int64_t repeats);

// ── Data Movement ──────────────────────────────────────────────────────────

// Copy src data into dst (must have same shape and dtype).
TSDEF ts_status ts_copy(ts_tensor* dst, const ts_tensor* src);

// Flatten to 1D (contiguous required). Returns a view if possible.
TS_NODISCARD TSDEF ts_status ts_flatten(ts_tensor** out, const ts_tensor* src);

// ── Thread Pool ─────────────────────────────────────────────────────────────

typedef struct ts_pool ts_pool;

// Create a thread pool with n worker threads. n=0 uses hardware concurrency.
TS_NODISCARD TSDEF ts_status ts_pool_create(ts_pool** out, int n_threads);

// Destroy thread pool (waits for all pending work to finish).
TSDEF void ts_pool_destroy(ts_pool* pool);

// Task function signature.
typedef void (*ts_task_fn)(void* arg);

// Submit a task for async execution.
TSDEF ts_status ts_pool_submit(ts_pool* pool, ts_task_fn fn, void* arg);

// Wait until all submitted tasks have completed.
TSDEF void ts_pool_wait(ts_pool* pool);

// Get number of threads in pool.
TSDEF int ts_pool_nthreads(const ts_pool* pool);

// ── Parallel Ops ────────────────────────────────────────────────────────────

// Parallel element-wise map (splits work across pool threads).
TS_NODISCARD TSDEF ts_status ts_parallel_map(ts_tensor** out,
                                             const ts_tensor* src,
                                             ts_map_fn fn, void* userdata,
                                             ts_pool* pool);

// Parallel for: calls fn(chunk_start, chunk_end, userdata) for each range.
// Divides [0, total) into pool_nthreads chunks.
typedef void (*ts_range_fn)(int64_t start, int64_t end, void* userdata);
TSDEF void ts_parallel_for(ts_pool* pool, int64_t total,
                           ts_range_fn fn, void* userdata);

// ── Creation Utilities ─────────────────────────────────────────────────────

// 1D tensor [0, 1, 2, ..., n-1] as I64.
TS_NODISCARD TSDEF ts_status ts_arange(ts_tensor** out, int64_t n);

// 1D tensor of n evenly spaced values from start to stop (inclusive) as F64.
TS_NODISCARD TSDEF ts_status ts_linspace(ts_tensor** out, double start,
                                         double stop, int64_t n);

// Identity matrix [n, n] as F64.
TS_NODISCARD TSDEF ts_status ts_eye(ts_tensor** out, int64_t n);

// ── Padding ────────────────────────────────────────────────────────────────

typedef enum ts_pad_mode {
    TS_PAD_ZERO = 0,     // pad with zeros
    TS_PAD_EDGE,         // replicate edge values
    TS_PAD_REFLECT,      // reflect about edge (not including edge)
} ts_pad_mode;

// Pad a tensor. pad_widths is [ndim][2] array of (before, after) per dim.
TS_NODISCARD TSDEF ts_status ts_pad(ts_tensor** out, const ts_tensor* src,
                                    const int64_t pad_widths[][2],
                                    ts_pad_mode mode);

// ── 3D Convolution ─────────────────────────────────────────────────────────

// 3D convolution with a small kernel. Output is same size as input (zero-padded).
// kernel must be a 3D tensor with odd dimensions.
TS_NODISCARD TSDEF ts_status ts_conv3d(ts_tensor** out, const ts_tensor* src,
                                       const ts_tensor* kernel);

// 3D Gaussian blur with given sigma. Constructs separable kernel internally.
TS_NODISCARD TSDEF ts_status ts_gaussian_blur3d(ts_tensor** out,
                                                const ts_tensor* src,
                                                double sigma);

// ── Statistics ──────────────────────────────────────────────────────────────

// Compute standard deviation over all elements.
TS_NODISCARD TSDEF ts_status ts_std(ts_tensor** out, const ts_tensor* src);

// Compute a histogram of values. Returns [n_bins] I64 tensor.
// range_lo/range_hi define the bin edges.
TS_NODISCARD TSDEF ts_status ts_histogram(ts_tensor** out, const ts_tensor* src,
                                          int64_t n_bins,
                                          double range_lo, double range_hi);

// ── Thresholding / Segmentation ────────────────────────────────────────────

// Binary threshold: out[i] = (src[i] > threshold) ? 1 : 0.  Output is TS_U8.
TS_NODISCARD TSDEF ts_status ts_threshold(ts_tensor** out, const ts_tensor* src,
                                          double threshold);

// Normalize to [0, 1] range as F32. out = (src - min) / (max - min).
TS_NODISCARD TSDEF ts_status ts_normalize(ts_tensor** out,
                                          const ts_tensor* src);


// ── Print / Debug ───────────────────────────────────────────────────────────

// Print tensor info and first few elements to FILE (pass stdout/stderr).
TSDEF void ts_print(const ts_tensor* t, FILE* f);

// Print shape string like "[3, 4, 5]" to buffer. Returns chars written.
TSDEF int ts_shape_str(const ts_tensor* t, char* buf, int bufsize);

// Print min/max/mean/std statistics.
TSDEF void ts_print_stats(const ts_tensor* t, FILE* f);

// ── Utilities ───────────────────────────────────────────────────────────────

TSDEF int64_t     ts_nelem(const ts_tensor* t);
TSDEF int64_t     ts_nbytes(const ts_tensor* t);
TSDEF bool        ts_is_contiguous(const ts_tensor* t);
TS_NODISCARD TSDEF ts_status ts_contiguous(ts_tensor** out,
                                           const ts_tensor* src);
TSDEF ts_status   ts_fill(ts_tensor* t, const void* value);
TSDEF const char* ts_status_str(ts_status s);
TSDEF const char* ts_version_str(void);

// ═══════════════════════════════════════════════════════════════════════════
// Implementation
// ═══════════════════════════════════════════════════════════════════════════

#ifdef TS_IMPLEMENTATION

#include <stdio.h>
#include <math.h>

// ── Internal helpers ────────────────────────────────────────────────────────

static const int64_t ts__dtype_sizes[TS_DTYPE_COUNT] = {
    [TS_U8]  = 1, [TS_U16] = 2, [TS_U32] = 4, [TS_U64] = 8,
    [TS_I8]  = 1, [TS_I16] = 2, [TS_I32] = 4, [TS_I64] = 8,
    [TS_F32] = 4, [TS_F64] = 8,
};

static const char* ts__dtype_names[TS_DTYPE_COUNT] = {
    [TS_U8]  = "uint8",  [TS_U16] = "uint16", [TS_U32] = "uint32", [TS_U64] = "uint64",
    [TS_I8]  = "int8",   [TS_I16] = "int16",  [TS_I32] = "int32",  [TS_I64] = "int64",
    [TS_F32] = "float32",[TS_F64] = "float64",
};

// Compute C-contiguous strides (row-major) from shape and dtype.
static void ts__compute_strides(int8_t ndim, const int64_t* shape,
                                int64_t elem_size, int64_t* strides)
{
    strides[ndim - 1] = elem_size;
    for (int8_t i = ndim - 2; i >= 0; i--)
        strides[i] = strides[i + 1] * shape[i + 1];
}

// Total number of elements from shape.
static int64_t ts__nelem(int8_t ndim, const int64_t* shape)
{
    int64_t n = 1;
    for (int8_t i = 0; i < ndim; i++)
        n *= shape[i];
    return n;
}

// Byte offset for a given index tuple.
static int64_t ts__offset(const ts_tensor* t, const int64_t* indices)
{
    int64_t off = 0;
    for (int8_t i = 0; i < t->ndim; i++)
        off += indices[i] * t->strides[i];
    return off;
}

// Allocate a tensor struct (not the data buffer).
static ts_tensor* ts__alloc_tensor(void)
{
    ts_tensor* t = (ts_tensor*)TS_CALLOC(1, sizeof(ts_tensor));
    return t;
}

// Read one element as double (for generic arithmetic).
static double ts__read_as_f64(const void* ptr, ts_dtype dtype)
{
    switch (dtype) {
        case TS_U8:  return (double)*(const uint8_t*)ptr;
        case TS_U16: return (double)*(const uint16_t*)ptr;
        case TS_U32: return (double)*(const uint32_t*)ptr;
        case TS_U64: return (double)*(const uint64_t*)ptr;
        case TS_I8:  return (double)*(const int8_t*)ptr;
        case TS_I16: return (double)*(const int16_t*)ptr;
        case TS_I32: return (double)*(const int32_t*)ptr;
        case TS_I64: return (double)*(const int64_t*)ptr;
        case TS_F32: return (double)*(const float*)ptr;
        case TS_F64: return *(const double*)ptr;
        default:     return 0.0;
    }
}

// Write a double into a typed element.
static void ts__write_from_f64(void* ptr, ts_dtype dtype, double val)
{
    switch (dtype) {
        case TS_U8:  *(uint8_t*)ptr  = (uint8_t)val;  break;
        case TS_U16: *(uint16_t*)ptr = (uint16_t)val;  break;
        case TS_U32: *(uint32_t*)ptr = (uint32_t)val;  break;
        case TS_U64: *(uint64_t*)ptr = (uint64_t)val;  break;
        case TS_I8:  *(int8_t*)ptr   = (int8_t)val;   break;
        case TS_I16: *(int16_t*)ptr  = (int16_t)val;  break;
        case TS_I32: *(int32_t*)ptr  = (int32_t)val;  break;
        case TS_I64: *(int64_t*)ptr  = (int64_t)val;  break;
        case TS_F32: *(float*)ptr    = (float)val;    break;
        case TS_F64: *(double*)ptr   = val;           break;
        default: break;
    }
}

// ── Multi-dimensional iterator ──────────────────────────────────────────────
// Iterates over all elements of a tensor, producing byte offsets.

typedef struct {
    int8_t   ndim;
    int64_t  shape[TS_MAX_DIM];
    int64_t  strides[TS_MAX_DIM];
    int64_t  coords[TS_MAX_DIM];
    int64_t  offset;
    int64_t  remaining;
} ts__iter;

static ts__iter ts__iter_start(const ts_tensor* t)
{
    ts__iter it = {0};
    it.ndim = t->ndim;
    it.offset = 0;
    it.remaining = ts__nelem(t->ndim, t->shape);
    for (int8_t i = 0; i < t->ndim; i++) {
        it.shape[i] = t->shape[i];
        it.strides[i] = t->strides[i];
        it.coords[i] = 0;
    }
    return it;
}

static bool ts__iter_valid(const ts__iter* it) { return it->remaining > 0; }

static void ts__iter_next(ts__iter* it)
{
    it->remaining--;
    for (int8_t i = it->ndim - 1; i >= 0; i--) {
        it->coords[i]++;
        it->offset += it->strides[i];
        if (it->coords[i] < it->shape[i])
            return;
        it->offset -= it->strides[i] * it->coords[i];
        it->coords[i] = 0;
    }
}

// ── Broadcasting iterator ───────────────────────────────────────────────────
// Iterates two tensors simultaneously with NumPy broadcasting.

typedef struct {
    int8_t   ndim;
    int64_t  shape[TS_MAX_DIM];
    int64_t  strides_a[TS_MAX_DIM];
    int64_t  strides_b[TS_MAX_DIM];
    int64_t  coords[TS_MAX_DIM];
    int64_t  off_a;
    int64_t  off_b;
    int64_t  remaining;
} ts__bcast_iter;

static ts_status ts__broadcast_shape(const ts_tensor* a, const ts_tensor* b,
                                     int8_t* out_ndim, int64_t* out_shape,
                                     int64_t* strides_a, int64_t* strides_b)
{
    int8_t ndim = a->ndim > b->ndim ? a->ndim : b->ndim;
    *out_ndim = ndim;

    for (int8_t i = 0; i < ndim; i++) {
        int8_t ai = i - (ndim - a->ndim);
        int8_t bi = i - (ndim - b->ndim);
        int64_t sa = (ai >= 0) ? a->shape[ai] : 1;
        int64_t sb = (bi >= 0) ? b->shape[bi] : 1;

        if (sa != sb && sa != 1 && sb != 1)
            return TS_ERR_SHAPE;

        out_shape[i] = sa > sb ? sa : sb;
        strides_a[i] = (ai >= 0 && sa > 1) ? a->strides[ai] : 0;
        strides_b[i] = (bi >= 0 && sb > 1) ? b->strides[bi] : 0;
    }
    return TS_OK;
}

static ts__bcast_iter ts__bcast_start(int8_t ndim, const int64_t* shape,
                                      const int64_t* sa, const int64_t* sb)
{
    ts__bcast_iter it = {0};
    it.ndim = ndim;
    it.off_a = 0;
    it.off_b = 0;
    it.remaining = 1;
    for (int8_t i = 0; i < ndim; i++) {
        it.shape[i] = shape[i];
        it.strides_a[i] = sa[i];
        it.strides_b[i] = sb[i];
        it.coords[i] = 0;
        it.remaining *= shape[i];
    }
    return it;
}

static void ts__bcast_next(ts__bcast_iter* it)
{
    it->remaining--;
    for (int8_t i = it->ndim - 1; i >= 0; i--) {
        it->coords[i]++;
        it->off_a += it->strides_a[i];
        it->off_b += it->strides_b[i];
        if (it->coords[i] < it->shape[i])
            return;
        it->off_a -= it->strides_a[i] * it->coords[i];
        it->off_b -= it->strides_b[i] * it->coords[i];
        it->coords[i] = 0;
    }
}

// ── Dtype helpers ───────────────────────────────────────────────────────────

TSDEF int64_t ts_dtype_size(ts_dtype d)
{
    if (d < 0 || d >= TS_DTYPE_COUNT) return 0;
    return ts__dtype_sizes[d];
}

TSDEF const char* ts_dtype_str(ts_dtype d)
{
    if (d < 0 || d >= TS_DTYPE_COUNT) return "unknown";
    return ts__dtype_names[d];
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

TSDEF ts_status ts_empty(ts_tensor** out, int8_t ndim,
                         const int64_t* shape, ts_dtype dtype)
{
    if (!out || !shape) return TS_ERR_NULL_ARG;
    if (ndim < 1 || ndim > TS_MAX_DIM) return TS_ERR_SHAPE;
    if (dtype < 0 || dtype >= TS_DTYPE_COUNT) return TS_ERR_DTYPE;

    for (int8_t i = 0; i < ndim; i++)
        if (shape[i] <= 0) return TS_ERR_SHAPE;

    ts_tensor* t = ts__alloc_tensor();
    if (!t) return TS_ERR_ALLOC;

    t->ndim = ndim;
    t->dtype = dtype;
    memcpy(t->shape, shape, ndim * sizeof(int64_t));
    ts__compute_strides(ndim, shape, ts__dtype_sizes[dtype], t->strides);

    int64_t nbytes = ts__nelem(ndim, shape) * ts__dtype_sizes[dtype];
    t->data = TS_MALLOC(nbytes);
    if (!t->data) { TS_FREE(t); return TS_ERR_ALLOC; }

    t->flags = TS_FLAG_OWNS_DATA | TS_FLAG_CONTIGUOUS;
    *out = t;
    return TS_OK;
}

TSDEF ts_status ts_zeros(ts_tensor** out, int8_t ndim,
                         const int64_t* shape, ts_dtype dtype)
{
    ts_status s = ts_empty(out, ndim, shape, dtype);
    if (s != TS_OK) return s;
    memset((*out)->data, 0,
           ts__nelem(ndim, shape) * ts__dtype_sizes[dtype]);
    return TS_OK;
}

TSDEF ts_status ts_ones(ts_tensor** out, int8_t ndim,
                        const int64_t* shape, ts_dtype dtype)
{
    ts_status s = ts_empty(out, ndim, shape, dtype);
    if (s != TS_OK) return s;

    double one = 1.0;
    return ts_fill(*out, &one);
}

TSDEF ts_status ts_full(ts_tensor** out, int8_t ndim, const int64_t* shape,
                        ts_dtype dtype, const void* fill_value)
{
    if (!fill_value) return TS_ERR_NULL_ARG;
    ts_status s = ts_empty(out, ndim, shape, dtype);
    if (s != TS_OK) return s;

    int64_t elem_sz = ts__dtype_sizes[dtype];
    int64_t n = ts__nelem(ndim, shape);
    char* p = (char*)(*out)->data;
    for (int64_t i = 0; i < n; i++)
        memcpy(p + i * elem_sz, fill_value, elem_sz);
    return TS_OK;
}

TSDEF ts_status ts_from_data(ts_tensor** out, void* data, int8_t ndim,
                             const int64_t* shape, ts_dtype dtype)
{
    if (!out || !data || !shape) return TS_ERR_NULL_ARG;
    if (ndim < 1 || ndim > TS_MAX_DIM) return TS_ERR_SHAPE;
    if (dtype < 0 || dtype >= TS_DTYPE_COUNT) return TS_ERR_DTYPE;

    for (int8_t i = 0; i < ndim; i++)
        if (shape[i] <= 0) return TS_ERR_SHAPE;

    ts_tensor* t = ts__alloc_tensor();
    if (!t) return TS_ERR_ALLOC;

    t->ndim = ndim;
    t->dtype = dtype;
    t->data = data;
    memcpy(t->shape, shape, ndim * sizeof(int64_t));
    ts__compute_strides(ndim, shape, ts__dtype_sizes[dtype], t->strides);
    t->flags = TS_FLAG_CONTIGUOUS; // does NOT own data
    *out = t;
    return TS_OK;
}

TSDEF ts_status ts_clone(ts_tensor** out, const ts_tensor* src)
{
    if (!out || !src) return TS_ERR_NULL_ARG;

    ts_tensor* t = ts__alloc_tensor();
    if (!t) return TS_ERR_ALLOC;

    int64_t n = ts__nelem(src->ndim, src->shape);
    int64_t elem_sz = ts__dtype_sizes[src->dtype];
    int64_t nbytes = n * elem_sz;

    t->data = TS_MALLOC(nbytes);
    if (!t->data) { TS_FREE(t); return TS_ERR_ALLOC; }

    t->ndim = src->ndim;
    t->dtype = src->dtype;
    memcpy(t->shape, src->shape, src->ndim * sizeof(int64_t));
    ts__compute_strides(src->ndim, src->shape, elem_sz, t->strides);
    t->flags = TS_FLAG_OWNS_DATA | TS_FLAG_CONTIGUOUS;

    if (src->flags & TS_FLAG_CONTIGUOUS) {
        memcpy(t->data, src->data, nbytes);
    } else {
        // Copy element-by-element for strided sources
        ts__iter it = ts__iter_start(src);
        char* dst = (char*)t->data;
        int64_t di = 0;
        while (ts__iter_valid(&it)) {
            memcpy(dst + di * elem_sz,
                   (const char*)src->data + it.offset, elem_sz);
            di++;
            ts__iter_next(&it);
        }
    }

    *out = t;
    return TS_OK;
}

TSDEF void ts_free(ts_tensor* t)
{
    if (!t) return;
    if (t->flags & TS_FLAG_OWNS_DATA)
        TS_FREE(t->data);
    TS_FREE(t);
}

// ── Element Access ──────────────────────────────────────────────────────────

TSDEF void* ts_ptr(const ts_tensor* t, const int64_t* indices)
{
    if (!t || !indices) return NULL;
    return (char*)t->data + ts__offset(t, indices);
}

TSDEF float ts_get_f32(const ts_tensor* t, const int64_t* indices)
{
    return (float)ts__read_as_f64(ts_ptr(t, indices), t->dtype);
}

TSDEF double ts_get_f64(const ts_tensor* t, const int64_t* indices)
{
    return ts__read_as_f64(ts_ptr(t, indices), t->dtype);
}

TSDEF uint8_t ts_get_u8(const ts_tensor* t, const int64_t* indices)
{
    return *(uint8_t*)ts_ptr(t, indices);
}

TSDEF uint16_t ts_get_u16(const ts_tensor* t, const int64_t* indices)
{
    return *(uint16_t*)ts_ptr(t, indices);
}

TSDEF int32_t ts_get_i32(const ts_tensor* t, const int64_t* indices)
{
    return *(int32_t*)ts_ptr(t, indices);
}

TSDEF int64_t ts_get_i64(const ts_tensor* t, const int64_t* indices)
{
    return *(int64_t*)ts_ptr(t, indices);
}

TSDEF void ts_set_f32(ts_tensor* t, const int64_t* indices, float val)
{
    ts__write_from_f64(ts_ptr(t, indices), t->dtype, (double)val);
}

TSDEF void ts_set_f64(ts_tensor* t, const int64_t* indices, double val)
{
    ts__write_from_f64(ts_ptr(t, indices), t->dtype, val);
}

TSDEF void ts_set_u8(ts_tensor* t, const int64_t* indices, uint8_t val)
{
    *(uint8_t*)ts_ptr(t, indices) = val;
}

TSDEF void ts_set_u16(ts_tensor* t, const int64_t* indices, uint16_t val)
{
    *(uint16_t*)ts_ptr(t, indices) = val;
}

TSDEF void ts_set_i32(ts_tensor* t, const int64_t* indices, int32_t val)
{
    *(int32_t*)ts_ptr(t, indices) = val;
}

TSDEF void ts_set_i64(ts_tensor* t, const int64_t* indices, int64_t val)
{
    *(int64_t*)ts_ptr(t, indices) = val;
}

// ── Views & Slicing ─────────────────────────────────────────────────────────

// Resolve a slice against a dimension size, computing start, length, step.
static void ts__resolve_slice(const ts_slice* s, int64_t dim_size,
                              int64_t* out_start, int64_t* out_len,
                              int64_t step)
{
    int64_t start = s->start;
    int64_t stop = s->stop;

    // Resolve negatives
    if (start < 0) start += dim_size;
    if (stop < 0)  stop += dim_size;

    // Clamp
    if (start < 0) start = 0;
    if (start > dim_size) start = dim_size;
    if (stop > dim_size) stop = dim_size;
    if (stop < 0) stop = 0;

    int64_t len = 0;
    if (step > 0 && stop > start)
        len = (stop - start + step - 1) / step;
    else if (step < 0 && start > stop)
        len = (start - stop - step - 1) / (-step);

    *out_start = start;
    *out_len = len;
}

TSDEF ts_status ts_view(ts_tensor** out, const ts_tensor* src,
                        const ts_slice* slices)
{
    if (!out || !src || !slices) return TS_ERR_NULL_ARG;

    ts_tensor* t = ts__alloc_tensor();
    if (!t) return TS_ERR_ALLOC;

    t->ndim = src->ndim;
    t->dtype = src->dtype;
    t->flags = 0; // view does not own data

    int64_t data_offset = 0;
    for (int8_t i = 0; i < src->ndim; i++) {
        int64_t start, len;
        int64_t step = slices[i].step;
        if (step == 0) { TS_FREE(t); return TS_ERR_SHAPE; }

        ts__resolve_slice(&slices[i], src->shape[i], &start, &len, step);

        t->shape[i] = len;
        t->strides[i] = src->strides[i] * step;
        data_offset += start * src->strides[i];
    }

    t->data = (char*)src->data + data_offset;

    // Check contiguity
    t->flags |= TS_FLAG_CONTIGUOUS;
    int64_t expected = ts__dtype_sizes[t->dtype];
    for (int8_t i = t->ndim - 1; i >= 0; i--) {
        if (t->shape[i] > 1 && t->strides[i] != expected) {
            t->flags &= ~TS_FLAG_CONTIGUOUS;
            break;
        }
        expected *= t->shape[i];
    }

    *out = t;
    return TS_OK;
}

TSDEF ts_status ts_reshape(ts_tensor** out, const ts_tensor* src,
                           int8_t ndim, const int64_t* new_shape)
{
    if (!out || !src || !new_shape) return TS_ERR_NULL_ARG;
    if (ndim < 1 || ndim > TS_MAX_DIM) return TS_ERR_SHAPE;
    if (!(src->flags & TS_FLAG_CONTIGUOUS)) return TS_ERR_STRIDE;

    int64_t src_n = ts__nelem(src->ndim, src->shape);
    int64_t dst_n = ts__nelem(ndim, new_shape);
    if (src_n != dst_n) return TS_ERR_SHAPE;

    ts_tensor* t = ts__alloc_tensor();
    if (!t) return TS_ERR_ALLOC;

    t->ndim = ndim;
    t->dtype = src->dtype;
    t->data = src->data;
    t->flags = TS_FLAG_CONTIGUOUS; // reshaped view, no ownership
    memcpy(t->shape, new_shape, ndim * sizeof(int64_t));
    ts__compute_strides(ndim, new_shape, ts__dtype_sizes[src->dtype],
                        t->strides);

    *out = t;
    return TS_OK;
}

TSDEF ts_status ts_transpose(ts_tensor** out, const ts_tensor* src,
                             const int8_t* axes)
{
    if (!out || !src) return TS_ERR_NULL_ARG;

    ts_tensor* t = ts__alloc_tensor();
    if (!t) return TS_ERR_ALLOC;

    t->ndim = src->ndim;
    t->dtype = src->dtype;
    t->data = src->data;
    t->flags = 0;

    if (axes) {
        // Validate axes are a permutation of 0..ndim-1
        bool seen[TS_MAX_DIM] = {0};
        for (int8_t i = 0; i < src->ndim; i++) {
            if (axes[i] < 0 || axes[i] >= src->ndim || seen[axes[i]]) {
                TS_FREE(t);
                return TS_ERR_SHAPE;
            }
            seen[axes[i]] = true;
            t->shape[i] = src->shape[axes[i]];
            t->strides[i] = src->strides[axes[i]];
        }
    } else {
        // Reverse all dims
        for (int8_t i = 0; i < src->ndim; i++) {
            t->shape[i] = src->shape[src->ndim - 1 - i];
            t->strides[i] = src->strides[src->ndim - 1 - i];
        }
    }

    // Check contiguity
    t->flags |= TS_FLAG_CONTIGUOUS;
    int64_t expected = ts__dtype_sizes[t->dtype];
    for (int8_t i = t->ndim - 1; i >= 0; i--) {
        if (t->shape[i] > 1 && t->strides[i] != expected) {
            t->flags &= ~TS_FLAG_CONTIGUOUS;
            break;
        }
        expected *= t->shape[i];
    }

    *out = t;
    return TS_OK;
}

TSDEF ts_status ts_squeeze(ts_tensor** out, const ts_tensor* src, int8_t axis)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    if (axis < 0 || axis >= src->ndim) return TS_ERR_BOUNDS;
    if (src->shape[axis] != 1) return TS_ERR_SHAPE;
    if (src->ndim <= 1) return TS_ERR_SHAPE;

    ts_tensor* t = ts__alloc_tensor();
    if (!t) return TS_ERR_ALLOC;

    t->ndim = src->ndim - 1;
    t->dtype = src->dtype;
    t->data = src->data;
    t->flags = src->flags & ~TS_FLAG_OWNS_DATA;

    int8_t j = 0;
    for (int8_t i = 0; i < src->ndim; i++) {
        if (i == axis) continue;
        t->shape[j] = src->shape[i];
        t->strides[j] = src->strides[i];
        j++;
    }

    *out = t;
    return TS_OK;
}

TSDEF ts_status ts_unsqueeze(ts_tensor** out, const ts_tensor* src, int8_t axis)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    if (axis < 0 || axis > src->ndim) return TS_ERR_BOUNDS;
    if (src->ndim >= TS_MAX_DIM) return TS_ERR_SHAPE;

    ts_tensor* t = ts__alloc_tensor();
    if (!t) return TS_ERR_ALLOC;

    t->ndim = src->ndim + 1;
    t->dtype = src->dtype;
    t->data = src->data;
    t->flags = src->flags & ~TS_FLAG_OWNS_DATA;

    int8_t j = 0;
    for (int8_t i = 0; i < t->ndim; i++) {
        if (i == axis) {
            t->shape[i] = 1;
            // stride for a size-1 dim doesn't matter for access,
            // but set it to be consistent with neighbors
            t->strides[i] = (j < src->ndim)
                ? src->strides[j] * src->shape[j]
                : ts__dtype_sizes[src->dtype];
        } else {
            t->shape[i] = src->shape[j];
            t->strides[i] = src->strides[j];
            j++;
        }
    }

    *out = t;
    return TS_OK;
}

// ── Elementwise Ops ─────────────────────────────────────────────────────────

typedef double (*ts__binop_fn)(double, double);

static double ts__op_add(double a, double b) { return a + b; }
static double ts__op_sub(double a, double b) { return a - b; }
static double ts__op_mul(double a, double b) { return a * b; }
static double ts__op_div(double a, double b) { return a / b; }

static ts_status ts__elementwise_binop(ts_tensor** out, const ts_tensor* a,
                                       const ts_tensor* b, ts__binop_fn op)
{
    if (!out || !a || !b) return TS_ERR_NULL_ARG;

    int8_t ndim;
    int64_t shape[TS_MAX_DIM];
    int64_t strides_a[TS_MAX_DIM], strides_b[TS_MAX_DIM];

    ts_status s = ts__broadcast_shape(a, b, &ndim, shape, strides_a, strides_b);
    if (s != TS_OK) return s;

    // Output dtype: promote to the "larger" type
    ts_dtype out_dtype = a->dtype > b->dtype ? a->dtype : b->dtype;

    ts_tensor* result;
    s = ts_empty(&result, ndim, shape, out_dtype);
    if (s != TS_OK) return s;

    ts__bcast_iter it = ts__bcast_start(ndim, shape, strides_a, strides_b);
    ts__iter oit = ts__iter_start(result);

    while (it.remaining > 0) {
        double va = ts__read_as_f64((const char*)a->data + it.off_a, a->dtype);
        double vb = ts__read_as_f64((const char*)b->data + it.off_b, b->dtype);
        double vr = op(va, vb);
        ts__write_from_f64((char*)result->data + oit.offset, out_dtype, vr);
        ts__bcast_next(&it);
        ts__iter_next(&oit);
    }

    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_add(ts_tensor** out, const ts_tensor* a, const ts_tensor* b)
{
    return ts__elementwise_binop(out, a, b, ts__op_add);
}

TSDEF ts_status ts_sub(ts_tensor** out, const ts_tensor* a, const ts_tensor* b)
{
    return ts__elementwise_binop(out, a, b, ts__op_sub);
}

TSDEF ts_status ts_mul(ts_tensor** out, const ts_tensor* a, const ts_tensor* b)
{
    return ts__elementwise_binop(out, a, b, ts__op_mul);
}

TSDEF ts_status ts_div(ts_tensor** out, const ts_tensor* a, const ts_tensor* b)
{
    return ts__elementwise_binop(out, a, b, ts__op_div);
}

TSDEF ts_status ts_scale(ts_tensor** out, const ts_tensor* src, double scalar)
{
    if (!out || !src) return TS_ERR_NULL_ARG;

    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, src->shape, src->dtype);
    if (s != TS_OK) return s;

    ts__iter si = ts__iter_start(src);
    ts__iter di = ts__iter_start(result);

    while (ts__iter_valid(&si)) {
        double v = ts__read_as_f64((const char*)src->data + si.offset,
                                    src->dtype);
        ts__write_from_f64((char*)result->data + di.offset,
                           result->dtype, v * scalar);
        ts__iter_next(&si);
        ts__iter_next(&di);
    }

    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_cast(ts_tensor** out, const ts_tensor* src, ts_dtype target)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    if (target < 0 || target >= TS_DTYPE_COUNT) return TS_ERR_DTYPE;

    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, src->shape, target);
    if (s != TS_OK) return s;

    ts__iter si = ts__iter_start(src);
    ts__iter di = ts__iter_start(result);

    while (ts__iter_valid(&si)) {
        double v = ts__read_as_f64((const char*)src->data + si.offset,
                                    src->dtype);
        ts__write_from_f64((char*)result->data + di.offset, target, v);
        ts__iter_next(&si);
        ts__iter_next(&di);
    }

    *out = result;
    return TS_OK;
}

// ── Reductions ──────────────────────────────────────────────────────────────

static ts_status ts__reduce(ts_tensor** out, const ts_tensor* src,
                            int8_t axis, int mode)
{
    // mode: 0=sum, 1=mean, 2=min, 3=max
    if (!out || !src) return TS_ERR_NULL_ARG;

    // Global reduction
    if (axis == -1) {
        int64_t scalar_shape[1] = {1};
        ts_dtype out_dtype = (mode == 1) ? TS_F64 : src->dtype;

        ts_tensor* result;
        ts_status s = ts_empty(&result, 1, scalar_shape, out_dtype);
        if (s != TS_OK) return s;

        ts__iter it = ts__iter_start(src);
        double acc;
        if (mode == 2)      acc = INFINITY;
        else if (mode == 3) acc = -INFINITY;
        else                acc = 0.0;

        int64_t count = 0;
        while (ts__iter_valid(&it)) {
            double v = ts__read_as_f64((const char*)src->data + it.offset,
                                        src->dtype);
            switch (mode) {
                case 0: case 1: acc += v; break;
                case 2: if (v < acc) acc = v; break;
                case 3: if (v > acc) acc = v; break;
            }
            count++;
            ts__iter_next(&it);
        }
        if (mode == 1 && count > 0) acc /= (double)count;

        ts__write_from_f64(result->data, out_dtype, acc);
        *out = result;
        return TS_OK;
    }

    // Per-axis reduction
    if (axis < 0 || axis >= src->ndim) return TS_ERR_BOUNDS;

    int8_t out_ndim = src->ndim;
    int64_t out_shape[TS_MAX_DIM];
    for (int8_t i = 0; i < src->ndim; i++)
        out_shape[i] = (i == axis) ? 1 : src->shape[i];

    ts_dtype out_dtype = (mode == 1) ? TS_F64 : src->dtype;

    ts_tensor* result;
    ts_status s = ts_zeros(&result, out_ndim, out_shape, out_dtype);
    if (s != TS_OK) return s;

    // Initialize min/max
    if (mode == 2 || mode == 3) {
        double init = (mode == 2) ? INFINITY : -INFINITY;
        ts__iter oi = ts__iter_start(result);
        while (ts__iter_valid(&oi)) {
            ts__write_from_f64((char*)result->data + oi.offset, out_dtype, init);
            ts__iter_next(&oi);
        }
    }

    // Iterate source, accumulate into result
    ts__iter it = ts__iter_start(src);
    while (ts__iter_valid(&it)) {
        // Compute output offset (same coords but axis coord = 0)
        int64_t out_off = 0;
        for (int8_t i = 0; i < src->ndim; i++) {
            int64_t c = (i == axis) ? 0 : it.coords[i];
            out_off += c * result->strides[i];
        }

        double v = ts__read_as_f64((const char*)src->data + it.offset,
                                    src->dtype);
        double cur = ts__read_as_f64((const char*)result->data + out_off,
                                     out_dtype);

        switch (mode) {
            case 0: case 1: cur += v; break;
            case 2: if (v < cur) cur = v; break;
            case 3: if (v > cur) cur = v; break;
        }
        ts__write_from_f64((char*)result->data + out_off, out_dtype, cur);
        ts__iter_next(&it);
    }

    // Divide by axis size for mean
    if (mode == 1) {
        double n = (double)src->shape[axis];
        ts__iter oi = ts__iter_start(result);
        while (ts__iter_valid(&oi)) {
            double v = ts__read_as_f64((char*)result->data + oi.offset,
                                        out_dtype);
            ts__write_from_f64((char*)result->data + oi.offset, out_dtype,
                               v / n);
            ts__iter_next(&oi);
        }
    }

    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_sum(ts_tensor** out, const ts_tensor* src, int8_t axis)
{
    return ts__reduce(out, src, axis, 0);
}

TSDEF ts_status ts_mean(ts_tensor** out, const ts_tensor* src, int8_t axis)
{
    return ts__reduce(out, src, axis, 1);
}

TSDEF ts_status ts_min(ts_tensor** out, const ts_tensor* src, int8_t axis)
{
    return ts__reduce(out, src, axis, 2);
}

TSDEF ts_status ts_max(ts_tensor** out, const ts_tensor* src, int8_t axis)
{
    return ts__reduce(out, src, axis, 3);
}

// ── Utilities ───────────────────────────────────────────────────────────────

TSDEF int64_t ts_nelem(const ts_tensor* t)
{
    if (!t) return 0;
    return ts__nelem(t->ndim, t->shape);
}

TSDEF int64_t ts_nbytes(const ts_tensor* t)
{
    if (!t) return 0;
    return ts_nelem(t) * ts__dtype_sizes[t->dtype];
}

TSDEF bool ts_is_contiguous(const ts_tensor* t)
{
    if (!t) return false;
    return (t->flags & TS_FLAG_CONTIGUOUS) != 0;
}

TSDEF ts_status ts_contiguous(ts_tensor** out, const ts_tensor* src)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    if (src->flags & TS_FLAG_CONTIGUOUS) {
        // Already contiguous — return a non-owning view
        ts_tensor* t = ts__alloc_tensor();
        if (!t) return TS_ERR_ALLOC;
        *t = *src;
        t->flags &= ~TS_FLAG_OWNS_DATA;
        *out = t;
        return TS_OK;
    }
    return ts_clone(out, src);
}

TSDEF ts_status ts_fill(ts_tensor* t, const void* value)
{
    if (!t || !value) return TS_ERR_NULL_ARG;

    // If fill_value is a raw element of the right dtype, use memcpy.
    // But we also support double* for convenience (ts_ones uses this).
    // We detect by checking if dtype is f64 and just always go through f64.
    double fill_val;
    if (t->dtype == TS_F64)
        fill_val = *(const double*)value;
    else
        fill_val = *(const double*)value; // caller passes double*

    ts__iter it = ts__iter_start(t);
    while (ts__iter_valid(&it)) {
        ts__write_from_f64((char*)t->data + it.offset, t->dtype, fill_val);
        ts__iter_next(&it);
    }
    return TS_OK;
}

TSDEF const char* ts_status_str(ts_status s)
{
    switch (s) {
        case TS_OK:              return "ok";
        case TS_ERR_NULL_ARG:    return "null argument";
        case TS_ERR_ALLOC:       return "allocation failed";
        case TS_ERR_SHAPE:       return "shape error";
        case TS_ERR_DTYPE:       return "dtype error";
        case TS_ERR_BOUNDS:      return "out of bounds";
        case TS_ERR_STRIDE:      return "non-contiguous";
        case TS_ERR_IO:          return "I/O error";
        case TS_ERR_UNSUPPORTED: return "unsupported";
        default:                 return "unknown error";
    }
}

TSDEF const char* ts_version_str(void)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d",
             TS_VERSION_MAJOR, TS_VERSION_MINOR, TS_VERSION_PATCH);
    return buf;
}

// ── Unary Math Ops ──────────────────────────────────────────────────────────

typedef double (*ts__unary_fn)(double);

static ts_status ts__unary_op(ts_tensor** out, const ts_tensor* src,
                              ts__unary_fn fn)
{
    if (!out || !src) return TS_ERR_NULL_ARG;

    ts_dtype out_dtype = src->dtype;
    // Promote integer types for sqrt/exp/log
    if (out_dtype < TS_F32) out_dtype = TS_F64;

    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, src->shape, out_dtype);
    if (s != TS_OK) return s;

    ts__iter si = ts__iter_start(src);
    ts__iter di = ts__iter_start(result);
    while (ts__iter_valid(&si)) {
        double v = ts__read_as_f64((const char*)src->data + si.offset, src->dtype);
        ts__write_from_f64((char*)result->data + di.offset, out_dtype, fn(v));
        ts__iter_next(&si);
        ts__iter_next(&di);
    }
    *out = result;
    return TS_OK;
}

static double ts__fn_sqrt(double v) { return sqrt(v); }
static double ts__fn_exp(double v) { return exp(v); }
static double ts__fn_log(double v) { return log(v); }

TSDEF ts_status ts_abs(ts_tensor** out, const ts_tensor* src)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    // abs preserves dtype (including integer)
    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, src->shape, src->dtype);
    if (s != TS_OK) return s;
    ts__iter si = ts__iter_start(src);
    ts__iter di = ts__iter_start(result);
    while (ts__iter_valid(&si)) {
        double v = ts__read_as_f64((const char*)src->data + si.offset, src->dtype);
        ts__write_from_f64((char*)result->data + di.offset, src->dtype, fabs(v));
        ts__iter_next(&si);
        ts__iter_next(&di);
    }
    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_neg(ts_tensor** out, const ts_tensor* src)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, src->shape, src->dtype);
    if (s != TS_OK) return s;
    ts__iter si = ts__iter_start(src);
    ts__iter di = ts__iter_start(result);
    while (ts__iter_valid(&si)) {
        double v = ts__read_as_f64((const char*)src->data + si.offset, src->dtype);
        ts__write_from_f64((char*)result->data + di.offset, src->dtype, -v);
        ts__iter_next(&si);
        ts__iter_next(&di);
    }
    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_sqrt(ts_tensor** out, const ts_tensor* src)
{ return ts__unary_op(out, src, ts__fn_sqrt); }

TSDEF ts_status ts_exp(ts_tensor** out, const ts_tensor* src)
{ return ts__unary_op(out, src, ts__fn_exp); }

TSDEF ts_status ts_log(ts_tensor** out, const ts_tensor* src)
{ return ts__unary_op(out, src, ts__fn_log); }

TSDEF ts_status ts_clamp(ts_tensor** out, const ts_tensor* src,
                         double lo, double hi)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, src->shape, src->dtype);
    if (s != TS_OK) return s;

    ts__iter si = ts__iter_start(src);
    ts__iter di = ts__iter_start(result);
    while (ts__iter_valid(&si)) {
        double v = ts__read_as_f64((const char*)src->data + si.offset, src->dtype);
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        ts__write_from_f64((char*)result->data + di.offset, src->dtype, v);
        ts__iter_next(&si);
        ts__iter_next(&di);
    }
    *out = result;
    return TS_OK;
}

// ── Comparison ──────────────────────────────────────────────────────────────

typedef bool (*ts__cmp_fn)(double, double);

static bool ts__cmp_eq(double a, double b) { return fabs(a - b) < 1e-12; }
static bool ts__cmp_gt(double a, double b) { return a > b; }
static bool ts__cmp_lt(double a, double b) { return a < b; }

static ts_status ts__compare(ts_tensor** out, const ts_tensor* a,
                             const ts_tensor* b, ts__cmp_fn fn)
{
    if (!out || !a || !b) return TS_ERR_NULL_ARG;

    int8_t ndim;
    int64_t shape[TS_MAX_DIM];
    int64_t strides_a[TS_MAX_DIM], strides_b[TS_MAX_DIM];

    ts_status s = ts__broadcast_shape(a, b, &ndim, shape, strides_a, strides_b);
    if (s != TS_OK) return s;

    ts_tensor* result;
    s = ts_zeros(&result, ndim, shape, TS_U8);
    if (s != TS_OK) return s;

    ts__bcast_iter it = ts__bcast_start(ndim, shape, strides_a, strides_b);
    ts__iter oit = ts__iter_start(result);

    while (it.remaining > 0) {
        double va = ts__read_as_f64((const char*)a->data + it.off_a, a->dtype);
        double vb = ts__read_as_f64((const char*)b->data + it.off_b, b->dtype);
        *((uint8_t*)((char*)result->data + oit.offset)) = fn(va, vb) ? 1 : 0;
        ts__bcast_next(&it);
        ts__iter_next(&oit);
    }

    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_eq(ts_tensor** out, const ts_tensor* a, const ts_tensor* b)
{ return ts__compare(out, a, b, ts__cmp_eq); }

TSDEF ts_status ts_gt(ts_tensor** out, const ts_tensor* a, const ts_tensor* b)
{ return ts__compare(out, a, b, ts__cmp_gt); }

TSDEF ts_status ts_lt(ts_tensor** out, const ts_tensor* a, const ts_tensor* b)
{ return ts__compare(out, a, b, ts__cmp_lt); }

// ── Where ───────────────────────────────────────────────────────────────────

TSDEF ts_status ts_where(ts_tensor** out, const ts_tensor* cond,
                         const ts_tensor* a, const ts_tensor* b)
{
    if (!out || !cond || !a || !b) return TS_ERR_NULL_ARG;
    if (cond->dtype != TS_U8) return TS_ERR_DTYPE;

    // Broadcast all three together — cond with a, then result with b
    int8_t ndim;
    int64_t shape[TS_MAX_DIM];
    int64_t sc[TS_MAX_DIM], sa[TS_MAX_DIM], sb[TS_MAX_DIM];

    // First broadcast cond and a
    int64_t shape_ca[TS_MAX_DIM], sca[TS_MAX_DIM], saa[TS_MAX_DIM];
    int8_t ndim_ca;
    ts_status s = ts__broadcast_shape(cond, a, &ndim_ca, shape_ca, sca, saa);
    if (s != TS_OK) return s;

    // Build a temporary shape holder to broadcast with b
    // We need to broadcast shape_ca with b's shape
    ndim = ndim_ca > b->ndim ? ndim_ca : b->ndim;
    for (int8_t i = 0; i < ndim; i++) {
        int8_t ci = i - (ndim - ndim_ca);
        int8_t bi = i - (ndim - b->ndim);
        int64_t s_ca = (ci >= 0) ? shape_ca[ci] : 1;
        int64_t s_b  = (bi >= 0) ? b->shape[bi] : 1;
        if (s_ca != s_b && s_ca != 1 && s_b != 1) return TS_ERR_SHAPE;
        shape[i] = s_ca > s_b ? s_ca : s_b;

        // Recompute strides for all three inputs against final shape
        int8_t cond_i = i - (ndim - cond->ndim);
        int8_t a_i    = i - (ndim - a->ndim);
        sc[i] = (cond_i >= 0 && cond->shape[cond_i] > 1) ? cond->strides[cond_i] : 0;
        sa[i] = (a_i >= 0 && a->shape[a_i] > 1) ? a->strides[a_i] : 0;
        sb[i] = (bi >= 0 && s_b > 1) ? b->strides[bi] : 0;
    }

    ts_dtype out_dtype = a->dtype > b->dtype ? a->dtype : b->dtype;
    ts_tensor* result;
    s = ts_empty(&result, ndim, shape, out_dtype);
    if (s != TS_OK) return s;

    // Iterate using manual coord tracking
    int64_t coords[TS_MAX_DIM] = {0};
    int64_t n = ts__nelem(ndim, shape);
    int64_t off_c = 0, off_a = 0, off_b = 0, off_o = 0;
    int64_t out_strides[TS_MAX_DIM];
    ts__compute_strides(ndim, shape, ts__dtype_sizes[out_dtype], out_strides);

    for (int64_t idx = 0; idx < n; idx++) {
        uint8_t c = *(const uint8_t*)((const char*)cond->data + off_c);
        double v;
        if (c)
            v = ts__read_as_f64((const char*)a->data + off_a, a->dtype);
        else
            v = ts__read_as_f64((const char*)b->data + off_b, b->dtype);
        ts__write_from_f64((char*)result->data + off_o, out_dtype, v);

        // Advance
        for (int8_t d = ndim - 1; d >= 0; d--) {
            coords[d]++;
            off_c += sc[d];
            off_a += sa[d];
            off_b += sb[d];
            off_o += out_strides[d];
            if (coords[d] < shape[d]) break;
            off_c -= sc[d] * coords[d];
            off_a -= sa[d] * coords[d];
            off_b -= sb[d] * coords[d];
            off_o -= out_strides[d] * coords[d];
            coords[d] = 0;
        }
    }

    *out = result;
    return TS_OK;
}

// ── Matmul ──────────────────────────────────────────────────────────────────

TSDEF ts_status ts_matmul(ts_tensor** out, const ts_tensor* a, const ts_tensor* b)
{
    if (!out || !a || !b) return TS_ERR_NULL_ARG;
    if (a->ndim != 2 || b->ndim != 2) return TS_ERR_SHAPE;
    if (a->shape[1] != b->shape[0]) return TS_ERR_SHAPE;

    int64_t M = a->shape[0];
    int64_t K = a->shape[1];
    int64_t N = b->shape[1];

    ts_dtype out_dtype = a->dtype > b->dtype ? a->dtype : b->dtype;
    if (out_dtype < TS_F32) out_dtype = TS_F64;

    int64_t shape[] = {M, N};
    ts_tensor* result;
    ts_status s = ts_zeros(&result, 2, shape, out_dtype);
    if (s != TS_OK) return s;

    for (int64_t i = 0; i < M; i++) {
        for (int64_t k = 0; k < K; k++) {
            int64_t aidx[] = {i, k};
            double aval = ts__read_as_f64(
                (const char*)a->data + ts__offset(a, aidx), a->dtype);
            for (int64_t j = 0; j < N; j++) {
                int64_t bidx[] = {k, j};
                int64_t oidx[] = {i, j};
                int64_t o_off = ts__offset(result, oidx);
                double bval = ts__read_as_f64(
                    (const char*)b->data + ts__offset(b, bidx), b->dtype);
                double cur = ts__read_as_f64(
                    (const char*)result->data + o_off, out_dtype);
                ts__write_from_f64(
                    (char*)result->data + o_off, out_dtype, cur + aval * bval);
            }
        }
    }

    *out = result;
    return TS_OK;
}

// ── Interpolation (3D volume sampling) ──────────────────────────────────────

// Cubic interpolation kernel (Catmull-Rom).
static double ts__cubic(double t, double p0, double p1, double p2, double p3)
{
    return p1 + 0.5 * t * (p2 - p0 +
           t * (2.0*p0 - 5.0*p1 + 4.0*p2 - p3 +
           t * (3.0*(p1 - p2) + p3 - p0)));
}

// Clamp integer index to [0, max-1].
static int64_t ts__clamp_idx(int64_t i, int64_t max)
{
    if (i < 0) return 0;
    if (i >= max) return max - 1;
    return i;
}

// Read a single voxel from a contiguous 3D tensor (any dtype) clamped.
static double ts__vol_at(const ts_tensor* vol, int64_t z, int64_t y, int64_t x)
{
    z = ts__clamp_idx(z, vol->shape[0]);
    y = ts__clamp_idx(y, vol->shape[1]);
    x = ts__clamp_idx(x, vol->shape[2]);
    int64_t off = z * vol->strides[0] + y * vol->strides[1] + x * vol->strides[2];
    return ts__read_as_f64((const char*)vol->data + off, vol->dtype);
}

TSDEF double ts_sample3d_at(const ts_tensor* vol,
                            double z, double y, double x,
                            ts_interp method)
{
    if (!vol || vol->ndim != 3) return 0.0;

    if (method == TS_NEAREST) {
        int64_t iz = (int64_t)(z + 0.5);
        int64_t iy = (int64_t)(y + 0.5);
        int64_t ix = (int64_t)(x + 0.5);
        return ts__vol_at(vol, iz, iy, ix);
    }

    if (method == TS_TRILINEAR) {
        double fz = floor(z), fy = floor(y), fx = floor(x);
        int64_t iz = (int64_t)fz, iy = (int64_t)fy, ix = (int64_t)fx;
        double dz = z - fz, dy = y - fy, dx = x - fx;

        double c000 = ts__vol_at(vol, iz,   iy,   ix);
        double c001 = ts__vol_at(vol, iz,   iy,   ix+1);
        double c010 = ts__vol_at(vol, iz,   iy+1, ix);
        double c011 = ts__vol_at(vol, iz,   iy+1, ix+1);
        double c100 = ts__vol_at(vol, iz+1, iy,   ix);
        double c101 = ts__vol_at(vol, iz+1, iy,   ix+1);
        double c110 = ts__vol_at(vol, iz+1, iy+1, ix);
        double c111 = ts__vol_at(vol, iz+1, iy+1, ix+1);

        double c00 = c000*(1-dx) + c001*dx;
        double c01 = c010*(1-dx) + c011*dx;
        double c10 = c100*(1-dx) + c101*dx;
        double c11 = c110*(1-dx) + c111*dx;

        double c0 = c00*(1-dy) + c01*dy;
        double c1 = c10*(1-dy) + c11*dy;

        return c0*(1-dz) + c1*dz;
    }

    // TS_TRICUBIC — Catmull-Rom separable
    {
        double fz = floor(z), fy = floor(y), fx = floor(x);
        int64_t iz = (int64_t)fz, iy = (int64_t)fy, ix = (int64_t)fx;
        double dz = z - fz, dy = y - fy, dx = x - fx;

        double slices[4]; // interpolate along z
        for (int dzi = -1; dzi <= 2; dzi++) {
            double rows[4]; // interpolate along y
            for (int dyi = -1; dyi <= 2; dyi++) {
                double p0 = ts__vol_at(vol, iz+dzi, iy+dyi, ix-1);
                double p1 = ts__vol_at(vol, iz+dzi, iy+dyi, ix);
                double p2 = ts__vol_at(vol, iz+dzi, iy+dyi, ix+1);
                double p3 = ts__vol_at(vol, iz+dzi, iy+dyi, ix+2);
                rows[dyi+1] = ts__cubic(dx, p0, p1, p2, p3);
            }
            slices[dzi+1] = ts__cubic(dy, rows[0], rows[1], rows[2], rows[3]);
        }
        return ts__cubic(dz, slices[0], slices[1], slices[2], slices[3]);
    }
}

TSDEF ts_status ts_sample3d(ts_tensor** out, const ts_tensor* vol,
                            const ts_tensor* coords, ts_interp method)
{
    if (!out || !vol || !coords) return TS_ERR_NULL_ARG;
    if (vol->ndim != 3) return TS_ERR_SHAPE;
    if (coords->ndim != 2 || coords->shape[1] != 3) return TS_ERR_SHAPE;
    if (coords->dtype != TS_F32 && coords->dtype != TS_F64) return TS_ERR_DTYPE;

    int64_t N = coords->shape[0];
    int64_t out_shape[] = {N};
    ts_dtype out_dtype = (vol->dtype == TS_F64) ? TS_F64 : TS_F32;

    ts_tensor* result;
    ts_status s = ts_empty(&result, 1, out_shape, out_dtype);
    if (s != TS_OK) return s;

    for (int64_t i = 0; i < N; i++) {
        int64_t ci0[] = {i, 0}, ci1[] = {i, 1}, ci2[] = {i, 2};
        double cz = ts__read_as_f64(
            (const char*)coords->data + ts__offset(coords, ci0), coords->dtype);
        double cy = ts__read_as_f64(
            (const char*)coords->data + ts__offset(coords, ci1), coords->dtype);
        double cx = ts__read_as_f64(
            (const char*)coords->data + ts__offset(coords, ci2), coords->dtype);

        double val = ts_sample3d_at(vol, cz, cy, cx, method);

        int64_t oi[] = {i};
        ts__write_from_f64((char*)result->data + ts__offset(result, oi),
                           out_dtype, val);
    }

    *out = result;
    return TS_OK;
}

// ── Downsample ──────────────────────────────────────────────────────────────

TSDEF ts_status ts_downsample3d_2x(ts_tensor** out, const ts_tensor* src)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    if (src->ndim != 3) return TS_ERR_SHAPE;

    int64_t sz = src->shape[0], sy = src->shape[1], sx = src->shape[2];
    if (sz < 2 || sy < 2 || sx < 2) return TS_ERR_SHAPE;

    int64_t out_shape[] = {sz / 2, sy / 2, sx / 2};
    ts_tensor* result;
    ts_status s = ts_empty(&result, 3, out_shape, src->dtype);
    if (s != TS_OK) return s;

    for (int64_t oz = 0; oz < out_shape[0]; oz++)
    for (int64_t oy = 0; oy < out_shape[1]; oy++)
    for (int64_t ox = 0; ox < out_shape[2]; ox++) {
        int64_t bz = oz*2, by = oy*2, bx = ox*2;
        double sum = 0;
        for (int dz = 0; dz < 2; dz++)
        for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++) {
            int64_t off = (bz+dz)*src->strides[0] +
                          (by+dy)*src->strides[1] +
                          (bx+dx)*src->strides[2];
            sum += ts__read_as_f64((const char*)src->data + off, src->dtype);
        }
        int64_t o_off = oz*result->strides[0] + oy*result->strides[1] +
                        ox*result->strides[2];
        ts__write_from_f64((char*)result->data + o_off, result->dtype,
                           sum / 8.0);
    }

    *out = result;
    return TS_OK;
}

// ── Paste ───────────────────────────────────────────────────────────────────

TSDEF ts_status ts_paste(ts_tensor* dst, const ts_tensor* src,
                         const int64_t* offset)
{
    if (!dst || !src || !offset) return TS_ERR_NULL_ARG;
    if (dst->ndim != src->ndim) return TS_ERR_SHAPE;
    if (dst->dtype != src->dtype) return TS_ERR_DTYPE;

    // Bounds check
    for (int8_t i = 0; i < src->ndim; i++) {
        if (offset[i] < 0 || offset[i] + src->shape[i] > dst->shape[i])
            return TS_ERR_BOUNDS;
    }

    ts__iter it = ts__iter_start(src);
    int64_t elem_sz = ts__dtype_sizes[src->dtype];

    while (ts__iter_valid(&it)) {
        // Compute dst offset
        int64_t dst_off = 0;
        for (int8_t i = 0; i < src->ndim; i++)
            dst_off += (it.coords[i] + offset[i]) * dst->strides[i];

        memcpy((char*)dst->data + dst_off,
               (const char*)src->data + it.offset, elem_sz);
        ts__iter_next(&it);
    }
    return TS_OK;
}

// ── Map ─────────────────────────────────────────────────────────────────────

TSDEF ts_status ts_map(ts_tensor** out, const ts_tensor* src,
                       ts_map_fn fn, void* userdata)
{
    if (!out || !src || !fn) return TS_ERR_NULL_ARG;

    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, src->shape, src->dtype);
    if (s != TS_OK) return s;

    ts__iter si = ts__iter_start(src);
    ts__iter di = ts__iter_start(result);

    while (ts__iter_valid(&si)) {
        double v = ts__read_as_f64((const char*)src->data + si.offset, src->dtype);
        v = fn(v, userdata);
        ts__write_from_f64((char*)result->data + di.offset, result->dtype, v);
        ts__iter_next(&si);
        ts__iter_next(&di);
    }

    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_map_inplace(ts_tensor* t, ts_map_fn fn, void* userdata)
{
    if (!t || !fn) return TS_ERR_NULL_ARG;

    ts__iter it = ts__iter_start(t);
    while (ts__iter_valid(&it)) {
        void* ptr = (char*)t->data + it.offset;
        double v = ts__read_as_f64(ptr, t->dtype);
        v = fn(v, userdata);
        ts__write_from_f64(ptr, t->dtype, v);
        ts__iter_next(&it);
    }
    return TS_OK;
}

// ── Virtual Tensor ──────────────────────────────────────────────────────────

struct ts_virtual {
    int64_t      shape[TS_MAX_DIM];
    int8_t       ndim;
    ts_dtype     dtype;
    ts_voxel_fn  voxel_fn;
    ts_region_fn region_fn;   // may be NULL
    void*        userdata;
};

TSDEF ts_status ts_virtual_create(ts_virtual** out, int8_t ndim,
                                  const int64_t* shape, ts_dtype dtype,
                                  ts_voxel_fn voxel_fn,
                                  ts_region_fn region_fn,
                                  void* userdata)
{
    if (!out || !shape || !voxel_fn) return TS_ERR_NULL_ARG;
    if (ndim < 1 || ndim > TS_MAX_DIM) return TS_ERR_SHAPE;

    ts_virtual* vt = (ts_virtual*)TS_CALLOC(1, sizeof(ts_virtual));
    if (!vt) return TS_ERR_ALLOC;

    vt->ndim = ndim;
    vt->dtype = dtype;
    vt->voxel_fn = voxel_fn;
    vt->region_fn = region_fn;
    vt->userdata = userdata;
    memcpy(vt->shape, shape, ndim * sizeof(int64_t));

    *out = vt;
    return TS_OK;
}

TSDEF void ts_virtual_free(ts_virtual* vt) { TS_FREE(vt); }

TSDEF double ts_virtual_get(ts_virtual* vt, const int64_t* coords)
{
    if (!vt || !coords) return 0.0;
    return vt->voxel_fn(coords, vt->userdata);
}

TSDEF double ts_virtual_get3(ts_virtual* vt, int64_t z, int64_t y, int64_t x)
{
    int64_t coords[] = {z, y, x};
    return ts_virtual_get(vt, coords);
}

TSDEF ts_status ts_virtual_read(ts_virtual* vt, ts_tensor** out,
                                const int64_t* start, const int64_t* stop)
{
    if (!vt || !out || !start || !stop) return TS_ERR_NULL_ARG;

    int64_t shape[TS_MAX_DIM];
    for (int8_t i = 0; i < vt->ndim; i++) {
        shape[i] = stop[i] - start[i];
        if (shape[i] <= 0) return TS_ERR_SHAPE;
    }

    ts_tensor* result;
    ts_status s = ts_empty(&result, vt->ndim, shape, vt->dtype);
    if (s != TS_OK) return s;

    // Try bulk read first
    if (vt->region_fn) {
        s = vt->region_fn(start, stop, result->data, vt->userdata);
        if (s == TS_OK) { *out = result; return TS_OK; }
    }

    // Fallback: per-voxel
    ts__iter it = ts__iter_start(result);
    while (ts__iter_valid(&it)) {
        int64_t coords[TS_MAX_DIM];
        for (int8_t d = 0; d < vt->ndim; d++)
            coords[d] = start[d] + it.coords[d];
        double v = vt->voxel_fn(coords, vt->userdata);
        ts__write_from_f64((char*)result->data + it.offset, vt->dtype, v);
        ts__iter_next(&it);
    }

    *out = result;
    return TS_OK;
}

TSDEF double ts_virtual_sample(ts_virtual* vt, double z, double y, double x,
                               ts_interp method)
{
    if (!vt || vt->ndim != 3) return 0.0;

    if (method == TS_NEAREST) {
        return ts_virtual_get3(vt, (int64_t)(z + 0.5),
                               (int64_t)(y + 0.5), (int64_t)(x + 0.5));
    }

    if (method == TS_TRILINEAR) {
        double fz = floor(z), fy = floor(y), fx = floor(x);
        int64_t iz = (int64_t)fz, iy = (int64_t)fy, ix = (int64_t)fx;
        double dz = z - fz, dy = y - fy, dx = x - fx;

        double c000 = ts_virtual_get3(vt, iz,   iy,   ix);
        double c001 = ts_virtual_get3(vt, iz,   iy,   ix+1);
        double c010 = ts_virtual_get3(vt, iz,   iy+1, ix);
        double c011 = ts_virtual_get3(vt, iz,   iy+1, ix+1);
        double c100 = ts_virtual_get3(vt, iz+1, iy,   ix);
        double c101 = ts_virtual_get3(vt, iz+1, iy,   ix+1);
        double c110 = ts_virtual_get3(vt, iz+1, iy+1, ix);
        double c111 = ts_virtual_get3(vt, iz+1, iy+1, ix+1);

        double c00 = c000*(1-dx) + c001*dx;
        double c01 = c010*(1-dx) + c011*dx;
        double c10 = c100*(1-dx) + c101*dx;
        double c11 = c110*(1-dx) + c111*dx;

        double c0 = c00*(1-dy) + c01*dy;
        double c1 = c10*(1-dy) + c11*dy;

        return c0*(1-dz) + c1*dz;
    }

    // Tricubic
    {
        double fz = floor(z), fy = floor(y), fx = floor(x);
        int64_t iz = (int64_t)fz, iy = (int64_t)fy, ix = (int64_t)fx;
        double dz = z - fz, dy = y - fy, dx = x - fx;

        double slices[4];
        for (int dzi = -1; dzi <= 2; dzi++) {
            double rows[4];
            for (int dyi = -1; dyi <= 2; dyi++) {
                double p0 = ts_virtual_get3(vt, iz+dzi, iy+dyi, ix-1);
                double p1 = ts_virtual_get3(vt, iz+dzi, iy+dyi, ix);
                double p2 = ts_virtual_get3(vt, iz+dzi, iy+dyi, ix+1);
                double p3 = ts_virtual_get3(vt, iz+dzi, iy+dyi, ix+2);
                rows[dyi+1] = ts__cubic(dx, p0, p1, p2, p3);
            }
            slices[dzi+1] = ts__cubic(dy, rows[0], rows[1], rows[2], rows[3]);
        }
        return ts__cubic(dz, slices[0], slices[1], slices[2], slices[3]);
    }
}

TSDEF ts_status ts_virtual_sample3d(ts_virtual* vt, ts_tensor** out,
                                    const ts_tensor* coords, ts_interp method)
{
    if (!vt || !out || !coords) return TS_ERR_NULL_ARG;
    if (vt->ndim != 3) return TS_ERR_SHAPE;
    if (coords->ndim != 2 || coords->shape[1] != 3) return TS_ERR_SHAPE;
    if (coords->dtype != TS_F32 && coords->dtype != TS_F64) return TS_ERR_DTYPE;

    int64_t N = coords->shape[0];
    int64_t out_shape[] = {N};
    ts_dtype out_dtype = TS_F64;

    ts_tensor* result;
    ts_status s = ts_empty(&result, 1, out_shape, out_dtype);
    if (s != TS_OK) return s;

    for (int64_t i = 0; i < N; i++) {
        int64_t ci0[] = {i, 0}, ci1[] = {i, 1}, ci2[] = {i, 2};
        double cz = ts__read_as_f64(
            (const char*)coords->data + ts__offset(coords, ci0), coords->dtype);
        double cy = ts__read_as_f64(
            (const char*)coords->data + ts__offset(coords, ci1), coords->dtype);
        double cx = ts__read_as_f64(
            (const char*)coords->data + ts__offset(coords, ci2), coords->dtype);

        double val = ts_virtual_sample(vt, cz, cy, cx, method);

        int64_t oi[] = {i};
        *(double*)((char*)result->data + ts__offset(result, oi)) = val;
    }

    *out = result;
    return TS_OK;
}

TSDEF int8_t ts_virtual_ndim(const ts_virtual* vt)
{ return vt ? vt->ndim : 0; }

TSDEF const int64_t* ts_virtual_shape(const ts_virtual* vt)
{ return vt ? vt->shape : NULL; }

TSDEF ts_dtype ts_virtual_dtype(const ts_virtual* vt)
{ return vt ? vt->dtype : TS_U8; }

// ── Print / Debug ───────────────────────────────────────────────────────────

TSDEF int ts_shape_str(const ts_tensor* t, char* buf, int bufsize)
{
    if (!t || !buf || bufsize < 3) return 0;
    int pos = 0;
    buf[pos++] = '[';
    for (int8_t i = 0; i < t->ndim; i++) {
        if (i > 0) {
            if (pos + 2 >= bufsize) break;
            buf[pos++] = ',';
            buf[pos++] = ' ';
        }
        pos += snprintf(buf + pos, bufsize - pos, "%lld",
                        (long long)t->shape[i]);
    }
    if (pos < bufsize) buf[pos++] = ']';
    if (pos < bufsize) buf[pos] = '\0';
    return pos;
}

TSDEF void ts_print(const ts_tensor* t, FILE* f)
{
    if (!t) { fprintf(f, "tensor(null)\n"); return; }

    char shape_buf[128];
    ts_shape_str(t, shape_buf, sizeof(shape_buf));

    fprintf(f, "tensor(shape=%s, dtype=%s", shape_buf, ts_dtype_str(t->dtype));
    if (!(t->flags & TS_FLAG_OWNS_DATA)) fprintf(f, ", view");
    if (!(t->flags & TS_FLAG_CONTIGUOUS)) fprintf(f, ", strided");

    int64_t n = ts_nelem(t);
    int64_t show = n < 10 ? n : 10;
    fprintf(f, ", data=[");

    ts__iter it = ts__iter_start(t);
    for (int64_t i = 0; i < show && ts__iter_valid(&it); i++) {
        double v = ts__read_as_f64((const char*)t->data + it.offset, t->dtype);
        if (i > 0) fprintf(f, ", ");
        if (t->dtype >= TS_F32)
            fprintf(f, "%.4g", v);
        else
            fprintf(f, "%lld", (long long)(int64_t)v);
        ts__iter_next(&it);
    }
    if (n > show) fprintf(f, ", ...");
    fprintf(f, "])\n");
}

// ── In-place Binary Ops ─────────────────────────────────────────────────────

static ts_status ts__inplace_binop(ts_tensor* a, const ts_tensor* b,
                                   ts__binop_fn op)
{
    if (!a || !b) return TS_ERR_NULL_ARG;

    int8_t ndim;
    int64_t shape[TS_MAX_DIM];
    int64_t strides_a[TS_MAX_DIM], strides_b[TS_MAX_DIM];

    ts_status s = ts__broadcast_shape(a, b, &ndim, shape, strides_a, strides_b);
    if (s != TS_OK) return s;

    // Output shape must match a's shape (can't grow in-place)
    if (ndim != a->ndim) return TS_ERR_SHAPE;
    for (int8_t i = 0; i < ndim; i++)
        if (shape[i] != a->shape[i]) return TS_ERR_SHAPE;

    ts__bcast_iter it = ts__bcast_start(ndim, shape, strides_a, strides_b);
    while (it.remaining > 0) {
        void* pa = (char*)a->data + it.off_a;
        double va = ts__read_as_f64(pa, a->dtype);
        double vb = ts__read_as_f64((const char*)b->data + it.off_b, b->dtype);
        ts__write_from_f64(pa, a->dtype, op(va, vb));
        ts__bcast_next(&it);
    }
    return TS_OK;
}

TSDEF ts_status ts_add_inplace(ts_tensor* a, const ts_tensor* b)
{ return ts__inplace_binop(a, b, ts__op_add); }

TSDEF ts_status ts_sub_inplace(ts_tensor* a, const ts_tensor* b)
{ return ts__inplace_binop(a, b, ts__op_sub); }

TSDEF ts_status ts_mul_inplace(ts_tensor* a, const ts_tensor* b)
{ return ts__inplace_binop(a, b, ts__op_mul); }

TSDEF ts_status ts_div_inplace(ts_tensor* a, const ts_tensor* b)
{ return ts__inplace_binop(a, b, ts__op_div); }

TSDEF ts_status ts_scale_inplace(ts_tensor* a, double scalar)
{
    if (!a) return TS_ERR_NULL_ARG;
    ts__iter it = ts__iter_start(a);
    while (ts__iter_valid(&it)) {
        void* p = (char*)a->data + it.offset;
        double v = ts__read_as_f64(p, a->dtype);
        ts__write_from_f64(p, a->dtype, v * scalar);
        ts__iter_next(&it);
    }
    return TS_OK;
}

// ── Linear Algebra ──────────────────────────────────────────────────────────

TSDEF ts_status ts_dot(ts_tensor** out, const ts_tensor* a, const ts_tensor* b)
{
    if (!out || !a || !b) return TS_ERR_NULL_ARG;
    if (a->ndim != 1 || b->ndim != 1) return TS_ERR_SHAPE;
    if (a->shape[0] != b->shape[0]) return TS_ERR_SHAPE;

    int64_t shape[] = {1};
    ts_tensor* result;
    ts_status s = ts_zeros(&result, 1, shape, TS_F64);
    if (s != TS_OK) return s;

    ts__iter ai = ts__iter_start(a);
    ts__iter bi = ts__iter_start(b);
    double acc = 0.0;
    while (ts__iter_valid(&ai)) {
        double va = ts__read_as_f64((const char*)a->data + ai.offset, a->dtype);
        double vb = ts__read_as_f64((const char*)b->data + bi.offset, b->dtype);
        acc += va * vb;
        ts__iter_next(&ai);
        ts__iter_next(&bi);
    }
    *(double*)result->data = acc;
    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_norm(ts_tensor** out, const ts_tensor* src)
{
    if (!out || !src) return TS_ERR_NULL_ARG;

    int64_t shape[] = {1};
    ts_tensor* result;
    ts_status s = ts_zeros(&result, 1, shape, TS_F64);
    if (s != TS_OK) return s;

    ts__iter it = ts__iter_start(src);
    double acc = 0.0;
    while (ts__iter_valid(&it)) {
        double v = ts__read_as_f64((const char*)src->data + it.offset, src->dtype);
        acc += v * v;
        ts__iter_next(&it);
    }
    *(double*)result->data = sqrt(acc);
    *out = result;
    return TS_OK;
}

// ── Argmin / Argmax ─────────────────────────────────────────────────────────

TSDEF int64_t ts_argmin(const ts_tensor* t)
{
    if (!t) return -1;
    ts__iter it = ts__iter_start(t);
    double best = INFINITY;
    int64_t best_idx = 0, idx = 0;
    while (ts__iter_valid(&it)) {
        double v = ts__read_as_f64((const char*)t->data + it.offset, t->dtype);
        if (v < best) { best = v; best_idx = idx; }
        idx++;
        ts__iter_next(&it);
    }
    return best_idx;
}

TSDEF int64_t ts_argmax(const ts_tensor* t)
{
    if (!t) return -1;
    ts__iter it = ts__iter_start(t);
    double best = -INFINITY;
    int64_t best_idx = 0, idx = 0;
    while (ts__iter_valid(&it)) {
        double v = ts__read_as_f64((const char*)t->data + it.offset, t->dtype);
        if (v > best) { best = v; best_idx = idx; }
        idx++;
        ts__iter_next(&it);
    }
    return best_idx;
}

// ── Concatenate / Stack ─────────────────────────────────────────────────────

TSDEF ts_status ts_cat(ts_tensor** out, const ts_tensor** tensors,
                       int n, int8_t axis)
{
    if (!out || !tensors || n <= 0) return TS_ERR_NULL_ARG;
    for (int i = 0; i < n; i++)
        if (!tensors[i]) return TS_ERR_NULL_ARG;

    const ts_tensor* first = tensors[0];
    if (axis < 0 || axis >= first->ndim) return TS_ERR_BOUNDS;

    // Validate shapes match on all dims except axis, compute total along axis
    int64_t total_axis = 0;
    for (int i = 0; i < n; i++) {
        if (tensors[i]->ndim != first->ndim) return TS_ERR_SHAPE;
        if (tensors[i]->dtype != first->dtype) return TS_ERR_DTYPE;
        for (int8_t d = 0; d < first->ndim; d++) {
            if (d == axis) continue;
            if (tensors[i]->shape[d] != first->shape[d]) return TS_ERR_SHAPE;
        }
        total_axis += tensors[i]->shape[axis];
    }

    int64_t out_shape[TS_MAX_DIM];
    for (int8_t d = 0; d < first->ndim; d++)
        out_shape[d] = (d == axis) ? total_axis : first->shape[d];

    ts_tensor* result;
    ts_status s = ts_empty(&result, first->ndim, out_shape, first->dtype);
    if (s != TS_OK) return s;

    int64_t offset[TS_MAX_DIM] = {0};
    for (int i = 0; i < n; i++) {
        ts_paste(result, tensors[i], offset);
        offset[axis] += tensors[i]->shape[axis];
    }

    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_stack(ts_tensor** out, const ts_tensor** tensors,
                         int n, int8_t axis)
{
    if (!out || !tensors || n <= 0) return TS_ERR_NULL_ARG;

    const ts_tensor* first = tensors[0];
    if (axis < 0 || axis > first->ndim) return TS_ERR_BOUNDS;
    if (first->ndim >= TS_MAX_DIM) return TS_ERR_SHAPE;

    // Validate all shapes identical
    for (int i = 1; i < n; i++) {
        if (tensors[i]->ndim != first->ndim) return TS_ERR_SHAPE;
        if (tensors[i]->dtype != first->dtype) return TS_ERR_DTYPE;
        for (int8_t d = 0; d < first->ndim; d++)
            if (tensors[i]->shape[d] != first->shape[d]) return TS_ERR_SHAPE;
    }

    // Build output shape with new axis
    int8_t out_ndim = first->ndim + 1;
    int64_t out_shape[TS_MAX_DIM];
    int8_t j = 0;
    for (int8_t d = 0; d < out_ndim; d++) {
        if (d == axis)
            out_shape[d] = n;
        else
            out_shape[d] = first->shape[j++];
    }

    ts_tensor* result;
    ts_status s = ts_empty(&result, out_ndim, out_shape, first->dtype);
    if (s != TS_OK) return s;

    // Copy each tensor into position along the new axis
    for (int i = 0; i < n; i++) {
        ts_tensor* usrc = NULL;
        ts_status us = ts_unsqueeze(&usrc, tensors[i], axis);
        if (us != TS_OK) { ts_free(result); return us; }
        int64_t offset[TS_MAX_DIM] = {0};
        offset[axis] = i;
        ts_paste(result, usrc, offset);
        ts_free(usrc);
    }

    *out = result;
    return TS_OK;
}

// ── Axis Manipulation ───────────────────────────────────────────────────────

TSDEF ts_status ts_flip(ts_tensor** out, const ts_tensor* src, int8_t axis)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    if (axis < 0 || axis >= src->ndim) return TS_ERR_BOUNDS;

    ts_tensor* t = ts__alloc_tensor();
    if (!t) return TS_ERR_ALLOC;

    *t = *src;
    t->flags &= ~TS_FLAG_OWNS_DATA;

    // Point data to the last element along this axis
    t->data = (char*)src->data + (src->shape[axis] - 1) * src->strides[axis];
    t->strides[axis] = -src->strides[axis];

    // Non-contiguous after flip (unless size-1)
    if (src->shape[axis] > 1)
        t->flags &= ~TS_FLAG_CONTIGUOUS;

    *out = t;
    return TS_OK;
}

TSDEF ts_status ts_repeat(ts_tensor** out, const ts_tensor* src,
                          int8_t axis, int64_t repeats)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    if (axis < 0 || axis >= src->ndim) return TS_ERR_BOUNDS;
    if (repeats <= 0) return TS_ERR_SHAPE;

    int64_t out_shape[TS_MAX_DIM];
    for (int8_t d = 0; d < src->ndim; d++)
        out_shape[d] = (d == axis) ? src->shape[d] * repeats : src->shape[d];

    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, out_shape, src->dtype);
    if (s != TS_OK) return s;

    int64_t offset[TS_MAX_DIM] = {0};
    for (int64_t r = 0; r < repeats; r++) {
        ts_paste(result, src, offset);
        offset[axis] += src->shape[axis];
    }

    *out = result;
    return TS_OK;
}

// ── Data Movement ───────────────────────────────────────────────────────────

TSDEF ts_status ts_copy(ts_tensor* dst, const ts_tensor* src)
{
    if (!dst || !src) return TS_ERR_NULL_ARG;
    if (dst->ndim != src->ndim) return TS_ERR_SHAPE;
    if (dst->dtype != src->dtype) return TS_ERR_DTYPE;
    for (int8_t i = 0; i < src->ndim; i++)
        if (dst->shape[i] != src->shape[i]) return TS_ERR_SHAPE;

    int64_t elem_sz = ts__dtype_sizes[src->dtype];

    if ((src->flags & TS_FLAG_CONTIGUOUS) &&
        (dst->flags & TS_FLAG_CONTIGUOUS)) {
        memcpy(dst->data, src->data,
               ts__nelem(src->ndim, src->shape) * elem_sz);
    } else {
        ts__iter si = ts__iter_start(src);
        ts__iter di = ts__iter_start(dst);
        while (ts__iter_valid(&si)) {
            memcpy((char*)dst->data + di.offset,
                   (const char*)src->data + si.offset, elem_sz);
            ts__iter_next(&si);
            ts__iter_next(&di);
        }
    }
    return TS_OK;
}

TSDEF ts_status ts_flatten(ts_tensor** out, const ts_tensor* src)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    int64_t n = ts__nelem(src->ndim, src->shape);
    int64_t flat_shape[] = {n};
    return ts_reshape(out, src, 1, flat_shape);
}

// ── Thread Pool ─────────────────────────────────────────────────────────────

#include <unistd.h>
#include <pthread.h>

// Ring buffer task queue
#define TS__POOL_QUEUE_CAP 4096

typedef struct {
    ts_task_fn fn;
    void*      arg;
} ts__task;

struct ts_pool {
    pthread_t*       threads;
    int              n_threads;
    ts__task         queue[TS__POOL_QUEUE_CAP];
    int              queue_head;
    int              queue_tail;
    int              queue_count;
    int              active;        // number of threads currently executing tasks
    pthread_mutex_t  mutex;
    pthread_cond_t   not_empty;     // signal workers
    pthread_cond_t   idle;          // signal wait()
    bool             shutdown;
};

static void* ts__pool_worker(void* arg)
{
    ts_pool* pool = (ts_pool*)arg;
    for (;;) {
        pthread_mutex_lock(&pool->mutex);
        while (pool->queue_count == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->not_empty, &pool->mutex);

        if (pool->shutdown && pool->queue_count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }

        ts__task task = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % TS__POOL_QUEUE_CAP;
        pool->queue_count--;
        pool->active++;
        pthread_mutex_unlock(&pool->mutex);

        task.fn(task.arg);

        pthread_mutex_lock(&pool->mutex);
        pool->active--;
        if (pool->queue_count == 0 && pool->active == 0)
            pthread_cond_broadcast(&pool->idle);
        pthread_mutex_unlock(&pool->mutex);
    }
}

TSDEF ts_status ts_pool_create(ts_pool** out, int n_threads)
{
    if (!out) return TS_ERR_NULL_ARG;

    if (n_threads <= 0) {
        // Detect hardware concurrency
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        n_threads = (n > 0) ? (int)n : 4;
    }

    ts_pool* pool = (ts_pool*)TS_CALLOC(1, sizeof(ts_pool));
    if (!pool) return TS_ERR_ALLOC;

    pool->n_threads = n_threads;
    pool->shutdown = false;
    pool->queue_head = pool->queue_tail = pool->queue_count = 0;
    pool->active = 0;
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->not_empty, NULL);
    pthread_cond_init(&pool->idle, NULL);

    pool->threads = (pthread_t*)TS_MALLOC(n_threads * sizeof(pthread_t));
    if (!pool->threads) {
        TS_FREE(pool);
        return TS_ERR_ALLOC;
    }

    for (int i = 0; i < n_threads; i++)
        pthread_create(&pool->threads[i], NULL, ts__pool_worker, pool);

    *out = pool;
    return TS_OK;
}

TSDEF void ts_pool_destroy(ts_pool* pool)
{
    if (!pool) return;

    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->n_threads; i++)
        pthread_join(pool->threads[i], NULL);

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->idle);
    TS_FREE(pool->threads);
    TS_FREE(pool);
}

TSDEF ts_status ts_pool_submit(ts_pool* pool, ts_task_fn fn, void* arg)
{
    if (!pool || !fn) return TS_ERR_NULL_ARG;

    pthread_mutex_lock(&pool->mutex);
    if (pool->queue_count >= TS__POOL_QUEUE_CAP) {
        pthread_mutex_unlock(&pool->mutex);
        return TS_ERR_ALLOC; // queue full
    }

    pool->queue[pool->queue_tail] = (ts__task){fn, arg};
    pool->queue_tail = (pool->queue_tail + 1) % TS__POOL_QUEUE_CAP;
    pool->queue_count++;

    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);
    return TS_OK;
}

TSDEF void ts_pool_wait(ts_pool* pool)
{
    if (!pool) return;
    pthread_mutex_lock(&pool->mutex);
    while (pool->queue_count > 0 || pool->active > 0)
        pthread_cond_wait(&pool->idle, &pool->mutex);
    pthread_mutex_unlock(&pool->mutex);
}

TSDEF int ts_pool_nthreads(const ts_pool* pool)
{
    return pool ? pool->n_threads : 0;
}

// ── Parallel Ops ────────────────────────────────────────────────────────────

typedef struct {
    const ts_tensor* src;
    ts_tensor*       dst;
    ts_map_fn        fn;
    void*            userdata;
    int64_t          start;
    int64_t          end;
} ts__pmap_arg;

static void ts__pmap_worker(void* arg)
{
    ts__pmap_arg* a = (ts__pmap_arg*)arg;
    int64_t elem_sz = ts__dtype_sizes[a->src->dtype];

    // For contiguous tensors, we can compute offsets directly
    if ((a->src->flags & TS_FLAG_CONTIGUOUS) &&
        (a->dst->flags & TS_FLAG_CONTIGUOUS)) {
        for (int64_t i = a->start; i < a->end; i++) {
            int64_t off = i * elem_sz;
            double v = ts__read_as_f64((const char*)a->src->data + off,
                                        a->src->dtype);
            v = a->fn(v, a->userdata);
            ts__write_from_f64((char*)a->dst->data + off, a->dst->dtype, v);
        }
    }
}

TSDEF ts_status ts_parallel_map(ts_tensor** out, const ts_tensor* src,
                                ts_map_fn fn, void* userdata,
                                ts_pool* pool)
{
    if (!out || !src || !fn || !pool) return TS_ERR_NULL_ARG;
    if (!(src->flags & TS_FLAG_CONTIGUOUS)) return TS_ERR_STRIDE;

    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, src->shape, src->dtype);
    if (s != TS_OK) return s;

    int64_t n = ts_nelem(src);
    int nt = pool->n_threads;
    if (nt > n) nt = (int)n;

    ts__pmap_arg* args = (ts__pmap_arg*)TS_MALLOC(nt * sizeof(ts__pmap_arg));
    if (!args) { ts_free(result); return TS_ERR_ALLOC; }

    int64_t chunk = n / nt;
    for (int i = 0; i < nt; i++) {
        args[i] = (ts__pmap_arg){
            .src = src, .dst = result, .fn = fn, .userdata = userdata,
            .start = i * chunk,
            .end = (i == nt - 1) ? n : (i + 1) * chunk,
        };
        ts_pool_submit(pool, ts__pmap_worker, &args[i]);
    }
    ts_pool_wait(pool);

    TS_FREE(args);
    *out = result;
    return TS_OK;
}

typedef struct {
    ts_range_fn fn;
    void*       userdata;
    int64_t     start;
    int64_t     end;
} ts__pfor_arg;

static void ts__pfor_worker(void* arg)
{
    ts__pfor_arg* a = (ts__pfor_arg*)arg;
    a->fn(a->start, a->end, a->userdata);
}

TSDEF void ts_parallel_for(ts_pool* pool, int64_t total,
                           ts_range_fn fn, void* userdata)
{
    if (!pool || !fn || total <= 0) return;

    int nt = pool->n_threads;
    if (nt > total) nt = (int)total;

    ts__pfor_arg* args = (ts__pfor_arg*)TS_MALLOC(nt * sizeof(ts__pfor_arg));
    if (!args) return;

    int64_t chunk = total / nt;
    for (int i = 0; i < nt; i++) {
        args[i] = (ts__pfor_arg){
            .fn = fn, .userdata = userdata,
            .start = i * chunk,
            .end = (i == nt - 1) ? total : (i + 1) * chunk,
        };
        ts_pool_submit(pool, ts__pfor_worker, &args[i]);
    }
    ts_pool_wait(pool);

    TS_FREE(args);
}

// ── Creation Utilities ──────────────────────────────────────────────────────

TSDEF ts_status ts_arange(ts_tensor** out, int64_t n)
{
    if (!out || n <= 0) return TS_ERR_NULL_ARG;
    int64_t shape[] = {n};
    ts_tensor* result;
    ts_status s = ts_empty(&result, 1, shape, TS_I64);
    if (s != TS_OK) return s;
    int64_t* data = (int64_t*)result->data;
    for (int64_t i = 0; i < n; i++) data[i] = i;
    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_linspace(ts_tensor** out, double start, double stop, int64_t n)
{
    if (!out || n <= 0) return TS_ERR_NULL_ARG;
    int64_t shape[] = {n};
    ts_tensor* result;
    ts_status s = ts_empty(&result, 1, shape, TS_F64);
    if (s != TS_OK) return s;
    double* data = (double*)result->data;
    if (n == 1) { data[0] = start; }
    else {
        double step = (stop - start) / (double)(n - 1);
        for (int64_t i = 0; i < n; i++) data[i] = start + i * step;
    }
    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_eye(ts_tensor** out, int64_t n)
{
    if (!out || n <= 0) return TS_ERR_NULL_ARG;
    int64_t shape[] = {n, n};
    ts_tensor* result;
    ts_status s = ts_zeros(&result, 2, shape, TS_F64);
    if (s != TS_OK) return s;
    for (int64_t i = 0; i < n; i++)
        ((double*)result->data)[i * n + i] = 1.0;
    *out = result;
    return TS_OK;
}

// ── Padding ─────────────────────────────────────────────────────────────────

TSDEF ts_status ts_pad(ts_tensor** out, const ts_tensor* src,
                       const int64_t pad_widths[][2], ts_pad_mode mode)
{
    if (!out || !src || !pad_widths) return TS_ERR_NULL_ARG;

    int64_t out_shape[TS_MAX_DIM];
    for (int8_t d = 0; d < src->ndim; d++) {
        out_shape[d] = pad_widths[d][0] + src->shape[d] + pad_widths[d][1];
        if (out_shape[d] <= 0) return TS_ERR_SHAPE;
    }

    ts_tensor* result;
    ts_status s = ts_zeros(&result, src->ndim, out_shape, src->dtype);
    if (s != TS_OK) return s;

    // Copy original data at offset
    int64_t offset[TS_MAX_DIM];
    for (int8_t d = 0; d < src->ndim; d++)
        offset[d] = pad_widths[d][0];
    ts_paste(result, src, offset);

    if (mode == TS_PAD_EDGE || mode == TS_PAD_REFLECT) {
        // Fill padded regions by iterating all output elements
        int64_t elem_sz = ts__dtype_sizes[src->dtype];
        ts__iter it = ts__iter_start(result);
        while (ts__iter_valid(&it)) {
            // Check if this element is in the padded region
            bool in_src = true;
            int64_t src_coords[TS_MAX_DIM];
            for (int8_t d = 0; d < src->ndim; d++) {
                int64_t c = it.coords[d] - pad_widths[d][0];
                if (c < 0 || c >= src->shape[d]) { in_src = false; }
                src_coords[d] = c;
            }

            if (!in_src) {
                // Compute source coordinate
                for (int8_t d = 0; d < src->ndim; d++) {
                    int64_t c = src_coords[d];
                    if (mode == TS_PAD_EDGE) {
                        if (c < 0) c = 0;
                        if (c >= src->shape[d]) c = src->shape[d] - 1;
                    } else { // REFLECT
                        while (c < 0 || c >= src->shape[d]) {
                            if (c < 0) c = -c;
                            if (c >= src->shape[d])
                                c = 2 * (src->shape[d] - 1) - c;
                        }
                    }
                    src_coords[d] = c;
                }
                // Read from source
                int64_t src_off = 0;
                for (int8_t d = 0; d < src->ndim; d++)
                    src_off += src_coords[d] * src->strides[d];
                memcpy((char*)result->data + it.offset,
                       (const char*)src->data + src_off, elem_sz);
            }
            ts__iter_next(&it);
        }
    }

    *out = result;
    return TS_OK;
}

// ── 3D Convolution ──────────────────────────────────────────────────────────

TSDEF ts_status ts_conv3d(ts_tensor** out, const ts_tensor* src,
                          const ts_tensor* kernel)
{
    if (!out || !src || !kernel) return TS_ERR_NULL_ARG;
    if (src->ndim != 3 || kernel->ndim != 3) return TS_ERR_SHAPE;
    // Kernel must have odd dimensions
    for (int8_t d = 0; d < 3; d++)
        if (kernel->shape[d] % 2 == 0) return TS_ERR_SHAPE;

    int64_t kz = kernel->shape[0], ky = kernel->shape[1], kx = kernel->shape[2];
    int64_t hz = kz/2, hy = ky/2, hx = kx/2;
    int64_t sz = src->shape[0], sy = src->shape[1], sx = src->shape[2];

    ts_tensor* result;
    ts_status s = ts_zeros(&result, 3, src->shape, TS_F64);
    if (s != TS_OK) return s;

    for (int64_t oz = 0; oz < sz; oz++)
    for (int64_t oy = 0; oy < sy; oy++)
    for (int64_t ox = 0; ox < sx; ox++) {
        double acc = 0.0;
        for (int64_t dz = 0; dz < kz; dz++)
        for (int64_t dy = 0; dy < ky; dy++)
        for (int64_t dx = 0; dx < kx; dx++) {
            int64_t iz = oz + dz - hz;
            int64_t iy = oy + dy - hy;
            int64_t ix = ox + dx - hx;
            if (iz >= 0 && iz < sz && iy >= 0 && iy < sy && ix >= 0 && ix < sx) {
                int64_t s_off = iz*src->strides[0] + iy*src->strides[1] +
                                ix*src->strides[2];
                int64_t k_off = dz*kernel->strides[0] + dy*kernel->strides[1] +
                                dx*kernel->strides[2];
                double sv = ts__read_as_f64((const char*)src->data + s_off,
                                             src->dtype);
                double kv = ts__read_as_f64((const char*)kernel->data + k_off,
                                             kernel->dtype);
                acc += sv * kv;
            }
        }
        int64_t o_off = oz*result->strides[0] + oy*result->strides[1] +
                        ox*result->strides[2];
        *(double*)((char*)result->data + o_off) = acc;
    }

    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_gaussian_blur3d(ts_tensor** out, const ts_tensor* src,
                                   double sigma)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    if (src->ndim != 3) return TS_ERR_SHAPE;
    if (sigma <= 0.0) return TS_ERR_SHAPE;

    // Build 3D Gaussian kernel
    int64_t radius = (int64_t)(3.0 * sigma + 0.5);
    if (radius < 1) radius = 1;
    int64_t ksize = 2 * radius + 1;
    int64_t kshape[] = {ksize, ksize, ksize};

    ts_tensor* kernel;
    ts_status s = ts_empty(&kernel, 3, kshape, TS_F64);
    if (s != TS_OK) return s;

    double sum = 0.0;
    double inv_2s2 = 1.0 / (2.0 * sigma * sigma);
    for (int64_t z = 0; z < ksize; z++)
    for (int64_t y = 0; y < ksize; y++)
    for (int64_t x = 0; x < ksize; x++) {
        double dz = (double)(z - radius);
        double dy = (double)(y - radius);
        double dx = (double)(x - radius);
        double v = exp(-(dz*dz + dy*dy + dx*dx) * inv_2s2);
        int64_t off = z*kernel->strides[0] + y*kernel->strides[1] +
                      x*kernel->strides[2];
        *(double*)((char*)kernel->data + off) = v;
        sum += v;
    }
    // Normalize
    ts_scale_inplace(kernel, 1.0 / sum);

    s = ts_conv3d(out, src, kernel);
    ts_free(kernel);
    return s;
}

// ── Statistics ──────────────────────────────────────────────────────────────

TSDEF ts_status ts_std(ts_tensor** out, const ts_tensor* src)
{
    if (!out || !src) return TS_ERR_NULL_ARG;

    int64_t n = ts_nelem(src);
    if (n == 0) return TS_ERR_SHAPE;

    // Two-pass: mean then variance
    ts__iter it = ts__iter_start(src);
    double sum = 0.0;
    while (ts__iter_valid(&it)) {
        sum += ts__read_as_f64((const char*)src->data + it.offset, src->dtype);
        ts__iter_next(&it);
    }
    double mean = sum / (double)n;

    it = ts__iter_start(src);
    double var_sum = 0.0;
    while (ts__iter_valid(&it)) {
        double v = ts__read_as_f64((const char*)src->data + it.offset, src->dtype);
        double d = v - mean;
        var_sum += d * d;
        ts__iter_next(&it);
    }

    int64_t shape[] = {1};
    ts_tensor* result;
    ts_status s = ts_empty(&result, 1, shape, TS_F64);
    if (s != TS_OK) return s;
    *(double*)result->data = sqrt(var_sum / (double)n);
    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_histogram(ts_tensor** out, const ts_tensor* src,
                             int64_t n_bins, double range_lo, double range_hi)
{
    if (!out || !src) return TS_ERR_NULL_ARG;
    if (n_bins <= 0 || range_hi <= range_lo) return TS_ERR_SHAPE;

    int64_t shape[] = {n_bins};
    ts_tensor* result;
    ts_status s = ts_zeros(&result, 1, shape, TS_I64);
    if (s != TS_OK) return s;

    double bin_width = (range_hi - range_lo) / (double)n_bins;
    int64_t* bins = (int64_t*)result->data;

    ts__iter it = ts__iter_start(src);
    while (ts__iter_valid(&it)) {
        double v = ts__read_as_f64((const char*)src->data + it.offset, src->dtype);
        int64_t bin = (int64_t)((v - range_lo) / bin_width);
        if (bin >= 0 && bin < n_bins)
            bins[bin]++;
        else if (v >= range_hi && bin == n_bins)
            bins[n_bins - 1]++; // inclusive upper edge
        ts__iter_next(&it);
    }

    *out = result;
    return TS_OK;
}

// ── Threshold / Normalize ───────────────────────────────────────────────────

TSDEF ts_status ts_threshold(ts_tensor** out, const ts_tensor* src,
                             double threshold)
{
    if (!out || !src) return TS_ERR_NULL_ARG;

    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, src->shape, TS_U8);
    if (s != TS_OK) return s;

    ts__iter si = ts__iter_start(src);
    ts__iter di = ts__iter_start(result);
    while (ts__iter_valid(&si)) {
        double v = ts__read_as_f64((const char*)src->data + si.offset, src->dtype);
        *(uint8_t*)((char*)result->data + di.offset) = (v > threshold) ? 1 : 0;
        ts__iter_next(&si);
        ts__iter_next(&di);
    }

    *out = result;
    return TS_OK;
}

TSDEF ts_status ts_normalize(ts_tensor** out, const ts_tensor* src)
{
    if (!out || !src) return TS_ERR_NULL_ARG;

    // Find min/max
    ts__iter it = ts__iter_start(src);
    double vmin = INFINITY, vmax = -INFINITY;
    while (ts__iter_valid(&it)) {
        double v = ts__read_as_f64((const char*)src->data + it.offset, src->dtype);
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        ts__iter_next(&it);
    }

    double range = vmax - vmin;
    if (range == 0.0) range = 1.0;

    ts_tensor* result;
    ts_status s = ts_empty(&result, src->ndim, src->shape, TS_F32);
    if (s != TS_OK) return s;

    ts__iter si = ts__iter_start(src);
    ts__iter di = ts__iter_start(result);
    while (ts__iter_valid(&si)) {
        double v = ts__read_as_f64((const char*)src->data + si.offset, src->dtype);
        *(float*)((char*)result->data + di.offset) = (float)((v - vmin) / range);
        ts__iter_next(&si);
        ts__iter_next(&di);
    }

    *out = result;
    return TS_OK;
}

// ── Print Stats ─────────────────────────────────────────────────────────────

TSDEF void ts_print_stats(const ts_tensor* t, FILE* f)
{
    if (!t || !f) return;

    char shape_buf[128];
    ts_shape_str(t, shape_buf, sizeof(shape_buf));

    int64_t n = ts_nelem(t);
    double vmin = INFINITY, vmax = -INFINITY, sum = 0.0;

    ts__iter it = ts__iter_start(t);
    while (ts__iter_valid(&it)) {
        double v = ts__read_as_f64((const char*)t->data + it.offset, t->dtype);
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        sum += v;
        ts__iter_next(&it);
    }
    double mean = sum / (double)n;

    it = ts__iter_start(t);
    double var_sum = 0.0;
    while (ts__iter_valid(&it)) {
        double v = ts__read_as_f64((const char*)t->data + it.offset, t->dtype);
        double d = v - mean;
        var_sum += d * d;
        ts__iter_next(&it);
    }
    double std = sqrt(var_sum / (double)n);

    fprintf(f, "tensor(shape=%s, dtype=%s, min=%.4g, max=%.4g, mean=%.4g, std=%.4g)\n",
            shape_buf, ts_dtype_str(t->dtype), vmin, vmax, mean, std);
}

#endif // TS_IMPLEMENTATION
#endif // TENSOR_H
