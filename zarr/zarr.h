// zarr.h — Pure C23 zarr library for volumetric X-ray data
// Built on c-blosc2 + vl264. Read any zarr, write one canonical format.
// Vesuvius Challenge / Villa Volume Cartographer.
#ifndef ZARR_H
#define ZARR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// ── Version ─────────────────────────────────────────────────────────────────

#define ZARR_VERSION_MAJOR 0
#define ZARR_VERSION_MINOR 1
#define ZARR_VERSION_PATCH 0

// ── Constants ───────────────────────────────────────────────────────────────

#define ZARR_CHUNK_DIM     128
#define ZARR_CHUNK_VOXELS  (128 * 128 * 128)          // 2,097,152
#define ZARR_SHARD_DIM     1024
#define ZARR_SHARD_CHUNKS  (8 * 8 * 8)                // 512 chunks per shard
#define ZARR_SHARD_VOXELS  (1024 * 1024 * 1024)       // 1,073,741,824
#define ZARR_MAX_DIM       8
#define ZARR_FILL_VALUE    0

// ── C23 Compat ──────────────────────────────────────────────────────────────

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define ZARR_NODISCARD    [[nodiscard]]
  #define ZARR_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define ZARR_NODISCARD    __attribute__((warn_unused_result))
  #define ZARR_MAYBE_UNUSED __attribute__((unused))
#else
  #define ZARR_NODISCARD
  #define ZARR_MAYBE_UNUSED
#endif

// ── Allocator Hooks ─────────────────────────────────────────────────────────

#ifndef ZARR_MALLOC
  #define ZARR_MALLOC(sz)     malloc(sz)
  #define ZARR_REALLOC(p, sz) realloc(p, sz)
  #define ZARR_FREE(p)        free(p)
  #define ZARR_CALLOC(n, sz)  calloc(n, sz)
#endif

// ── Status Codes ────────────────────────────────────────────────────────────

typedef enum zarr_status {
    ZARR_OK = 0,
    ZARR_ERR_NULL_ARG,
    ZARR_ERR_ALLOC,
    ZARR_ERR_IO,
    ZARR_ERR_FORMAT,        // unrecognized or corrupt zarr metadata
    ZARR_ERR_DTYPE,         // unsupported source dtype
    ZARR_ERR_CODEC,         // compression/decompression failure
    ZARR_ERR_BOUNDS,        // index out of range
    ZARR_ERR_UNSUPPORTED,   // unsupported zarr feature
    ZARR_ERR_BLOSC,         // blosc2 error
    ZARR_ERR_VL264,         // vl264 error
} zarr_status;

// ── Array Handle ────────────────────────────────────────────────────────────

typedef struct zarr_array zarr_array;

// ── Lifecycle ───────────────────────────────────────────────────────────────

// Call once at startup: registers vl264 codec with blosc2.
ZARR_NODISCARD zarr_status zarr_init(void);

// Call at shutdown: cleanup.
void zarr_destroy(void);

// Create a new canonical array on disk.
// ndim must be 1..8. Shape elements must be > 0.
// Stores as b2nd with vl264 codec, 128³ chunks.
ZARR_NODISCARD zarr_status zarr_create(zarr_array** out, const char* path,
                                       int8_t ndim, const int64_t* shape);

// Open an existing canonical array (b2nd-backed with vl264).
ZARR_NODISCARD zarr_status zarr_open(zarr_array** out, const char* path);

// Import a foreign zarr (v2 or v3, any dtype/codec) and convert to canonical format.
// The converted array is cached at cache_path for future use.
ZARR_NODISCARD zarr_status zarr_ingest(zarr_array** out, const char* src_path,
                                       const char* cache_path);

// Close array and free resources.
void zarr_close(zarr_array* arr);

// ── Data Access ─────────────────────────────────────────────────────────────

// Read a sub-region [start, stop) into caller buffer (uint8_t, row-major).
// start/stop arrays must have ndim elements.
ZARR_NODISCARD zarr_status zarr_read(const zarr_array* arr,
                                     const int64_t* start,
                                     const int64_t* stop,
                                     uint8_t* buf, int64_t bufsize);

// Write a sub-region [start, stop) from caller buffer (uint8_t, row-major).
ZARR_NODISCARD zarr_status zarr_write(zarr_array* arr,
                                      const int64_t* start,
                                      const int64_t* stop,
                                      const uint8_t* buf, int64_t bufsize);

// Read a single 128³ chunk by chunk coordinates.
// chunk_coords: ndim-element array of chunk indices (not voxel coords).
ZARR_NODISCARD zarr_status zarr_read_chunk(const zarr_array* arr,
                                           const int64_t* chunk_coords,
                                           uint8_t* buf);

// Write a single 128³ chunk by chunk coordinates.
ZARR_NODISCARD zarr_status zarr_write_chunk(zarr_array* arr,
                                            const int64_t* chunk_coords,
                                            const uint8_t* buf);

// ── Metadata ────────────────────────────────────────────────────────────────

int8_t          zarr_ndim(const zarr_array* arr);
const int64_t*  zarr_shape(const zarr_array* arr);
int64_t         zarr_nchunks(const zarr_array* arr);

// Write a zarr v3 zarr.json metadata file for interoperability.
ZARR_NODISCARD zarr_status zarr_write_v3_metadata(const zarr_array* arr,
                                                  const char* path);

// ── Statistics ──────────────────────────────────────────────────────────────

typedef struct zarr_stats {
    double   mean;
    double   stddev;
    uint8_t  min;
    uint8_t  max;
    int64_t  count;       // number of voxels sampled
    int64_t  zero_count;  // number of zero voxels
} zarr_stats;

// Compute stats over a sub-region [start, stop).
ZARR_NODISCARD zarr_status zarr_compute_stats(const zarr_array* arr,
                                              const int64_t* start,
                                              const int64_t* stop,
                                              zarr_stats* stats);

// ── Region Operations ───────────────────────────────────────────────────────

// Fill a sub-region [start, stop) with a constant value.
ZARR_NODISCARD zarr_status zarr_fill(zarr_array* arr,
                                     const int64_t* start,
                                     const int64_t* stop,
                                     uint8_t value);

// Copy a region from src to dst. Both must have the same ndim.
// src_start/src_stop define the source region.
// dst_start defines where to place it in the destination.
ZARR_NODISCARD zarr_status zarr_copy_region(const zarr_array* src,
                                            const int64_t* src_start,
                                            const int64_t* src_stop,
                                            zarr_array* dst,
                                            const int64_t* dst_start);

// ── Compression Info ────────────────────────────────────────────────────────

// Get on-disk compressed size in bytes (approximate — sum of all stored chunks).
ZARR_NODISCARD zarr_status zarr_compressed_size(const zarr_array* arr,
                                                int64_t* out_bytes);

// Get compression ratio (uncompressed / compressed). Returns 0 on error.
double zarr_compression_ratio(const zarr_array* arr);

// ── Encoder Configuration ───────────────────────────────────────────────────

// Reconfigure vl264 encoder quality. Takes effect for subsequent writes.
// quality: 0=FAST, 1=DEFAULT, 2=MAX
ZARR_NODISCARD zarr_status zarr_set_quality(int quality);

// Get the path of an open array.
const char* zarr_array_path(const zarr_array* arr);

// ── Slice Extraction ────────────────────────────────────────────────────────

typedef enum zarr_axis {
    ZARR_AXIS_0 = 0,  // first dimension (Z in ZYX)
    ZARR_AXIS_1 = 1,  // second dimension (Y in ZYX)
    ZARR_AXIS_2 = 2,  // third dimension (X in ZYX)
} zarr_axis;

// Extract a 2D slice from a 3D array along the given axis at the given index.
// For a ZYX array: axis=0 → YX plane, axis=1 → ZX plane, axis=2 → ZY plane.
// buf must be pre-allocated: product of the two non-sliced dimensions.
ZARR_NODISCARD zarr_status zarr_slice(const zarr_array* arr,
                                      zarr_axis axis, int64_t index,
                                      uint8_t* buf, int64_t bufsize);

// ── Downsampling / LOD ──────────────────────────────────────────────────────

// Create a 2x-downsampled copy of src at dst_path.
// Each output voxel is the mean of the 2x2x2 neighborhood.
// Output shape is ceil(src_shape / 2) per dimension.
// Only supports 3D arrays.
ZARR_NODISCARD zarr_status zarr_downsample_2x(const zarr_array* src,
                                              const char* dst_path,
                                              zarr_array** out);

// ── Histogram ───────────────────────────────────────────────────────────────

typedef struct zarr_histogram {
    uint64_t bins[256];   // count of each value 0..255
    int64_t  total;       // sum of all bins
    uint8_t  percentile_1;
    uint8_t  percentile_5;
    uint8_t  percentile_50;
    uint8_t  percentile_95;
    uint8_t  percentile_99;
} zarr_histogram;

// Compute histogram over a sub-region [start, stop).
ZARR_NODISCARD zarr_status zarr_compute_histogram(const zarr_array* arr,
                                                  const int64_t* start,
                                                  const int64_t* stop,
                                                  zarr_histogram* hist);

// ── Chunk Iteration ─────────────────────────────────────────────────────────

// Callback for zarr_foreach_chunk. coords is ndim-element chunk index array.
// data is the chunk voxel data (128³ u8, may be smaller at edges).
// nvoxels is the number of valid voxels. userdata is passed through.
// Return ZARR_OK to continue, any other status to stop iteration.
typedef zarr_status (*zarr_chunk_fn)(const int64_t* coords,
                                    const uint8_t* data,
                                    int64_t nvoxels,
                                    void* userdata);

// Iterate over all chunks, calling fn for each.
ZARR_NODISCARD zarr_status zarr_foreach_chunk(const zarr_array* arr,
                                              zarr_chunk_fn fn,
                                              void* userdata);

// ── Resize ──────────────────────────────────────────────────────────────────

// Resize an array in-place. new_shape must have ndim elements.
// Expanded regions are zero-filled. Shrunk regions are truncated.
ZARR_NODISCARD zarr_status zarr_resize(zarr_array* arr,
                                       const int64_t* new_shape);

// ── Projections ─────────────────────────────────────────────────────────────

// Max intensity projection along an axis over [start, stop).
// Output is a 2D image: product of the two non-projected dimensions.
ZARR_NODISCARD zarr_status zarr_project_max(const zarr_array* arr,
                                            zarr_axis axis,
                                            int64_t start_idx, int64_t stop_idx,
                                            uint8_t* buf, int64_t bufsize);

// Mean intensity projection along an axis over [start, stop).
ZARR_NODISCARD zarr_status zarr_project_mean(const zarr_array* arr,
                                             zarr_axis axis,
                                             int64_t start_idx, int64_t stop_idx,
                                             uint8_t* buf, int64_t bufsize);

// ── Comparison ──────────────────────────────────────────────────────────────

typedef struct zarr_diff {
    double   mse;          // mean squared error
    double   psnr;         // peak signal-to-noise ratio (dB), INFINITY if identical
    double   mae;          // mean absolute error
    uint8_t  max_abs_err;  // maximum absolute difference
    int64_t  count;        // number of voxels compared
    int64_t  diff_count;   // number of voxels that differ
} zarr_diff;

// Compare two arrays (or regions) voxel-by-voxel.
// Both must have the same ndim. Regions must be the same size.
ZARR_NODISCARD zarr_status zarr_compare(const zarr_array* a,
                                        const int64_t* a_start,
                                        const int64_t* a_stop,
                                        const zarr_array* b,
                                        const int64_t* b_start,
                                        zarr_diff* diff);

// ── Threshold / Bounding Box ────────────────────────────────────────────────

// Find axis-aligned bounding box of voxels >= threshold in [start, stop).
// Returns ZARR_OK and sets bb_start/bb_stop, or ZARR_ERR_BOUNDS if no voxels found.
ZARR_NODISCARD zarr_status zarr_bounding_box(const zarr_array* arr,
                                             const int64_t* start,
                                             const int64_t* stop,
                                             uint8_t threshold,
                                             int64_t* bb_start,
                                             int64_t* bb_stop);

// Count voxels >= threshold in [start, stop).
ZARR_NODISCARD zarr_status zarr_count_above(const zarr_array* arr,
                                            const int64_t* start,
                                            const int64_t* stop,
                                            uint8_t threshold,
                                            int64_t* count);

// ── Apply LUT ───────────────────────────────────────────────────────────────

// Apply a 256-entry lookup table to remap voxel values in [start, stop).
// lut[old_value] = new_value. Operates in-place on the array.
ZARR_NODISCARD zarr_status zarr_apply_lut(zarr_array* arr,
                                          const int64_t* start,
                                          const int64_t* stop,
                                          const uint8_t lut[256]);

// ── LOD Pyramid ─────────────────────────────────────────────────────────────

// Build a full multi-resolution pyramid. Creates arrays at:
//   base_path/0 (full res), base_path/1 (2x downsample), base_path/2 (4x), ...
// Stops when any dimension reaches 1. Returns number of levels.
// out_levels is an array of zarr_array* pointers, caller must close each.
// max_levels is the size of the out_levels array.
ZARR_NODISCARD zarr_status zarr_build_pyramid(const zarr_array* src,
                                              const char* base_path,
                                              zarr_array** out_levels,
                                              int max_levels,
                                              int* num_levels);

// ── Groups & Hierarchy ──────────────────────────────────────────────────────

typedef struct zarr_group zarr_group;

// Open a zarr group (v2 or v3). Reads .zgroup/.zattrs or zarr.json.
ZARR_NODISCARD zarr_status zarr_group_open(zarr_group** out, const char* path);

// Close group and free resources.
void zarr_group_close(zarr_group* grp);

// Get the zarr format version (2 or 3).
int zarr_group_version(const zarr_group* grp);

// Get the group path.
const char* zarr_group_path(const zarr_group* grp);

// List child names (arrays and subgroups). Returns count.
// names is an array of char* pointers — caller must free each name and the array.
ZARR_NODISCARD zarr_status zarr_group_list(const zarr_group* grp,
                                           char*** names, int* count);

// Check if a child is an array (vs a subgroup).
bool zarr_group_is_array(const zarr_group* grp, const char* name);

// Open a child array by name. Shorthand for zarr_open(grp_path/name).
ZARR_NODISCARD zarr_status zarr_group_open_array(const zarr_group* grp,
                                                 const char* name,
                                                 zarr_array** out);

// ── Attributes ──────────────────────────────────────────────────────────────

// Read a string attribute. Returns nullptr if not found.
// Caller must free the returned string.
char* zarr_attr_get_string(const zarr_group* grp, const char* key);

// Read an integer attribute. Returns default_val if not found.
int64_t zarr_attr_get_int(const zarr_group* grp, const char* key, int64_t default_val);

// Read a float attribute. Returns default_val if not found.
double zarr_attr_get_float(const zarr_group* grp, const char* key, double default_val);

// ── Consolidated Metadata ───────────────────────────────────────────────────

// Open a zarr store with consolidated metadata.
// For v2: reads .zmetadata at root.
// For v3: reads consolidated metadata from root zarr.json.
// Falls back to non-consolidated if not found.
ZARR_NODISCARD zarr_status zarr_group_open_consolidated(zarr_group** out,
                                                        const char* path);

// ── Crop ────────────────────────────────────────────────────────────────────

// Create a new array containing a sub-region of src.
ZARR_NODISCARD zarr_status zarr_crop(const zarr_array* src,
                                     const int64_t* start,
                                     const int64_t* stop,
                                     const char* dst_path,
                                     zarr_array** out);

// ── Window / Level ──────────────────────────────────────────────────────────

// Remap intensity range [win_low, win_high] → [0, 255] in [start, stop).
// Values below win_low become 0, above win_high become 255.
ZARR_NODISCARD zarr_status zarr_window_level(zarr_array* arr,
                                             const int64_t* start,
                                             const int64_t* stop,
                                             uint8_t win_low, uint8_t win_high);

// ── 3D Box Blur ─────────────────────────────────────────────────────────────

// Apply a 3D box blur with the given radius (kernel = 2*radius+1 per axis).
// src and dst may be the same array for in-place operation.
// Only supports 3D. radius must be >= 1.
ZARR_NODISCARD zarr_status zarr_box_blur(const zarr_array* src,
                                         const int64_t* start,
                                         const int64_t* stop,
                                         int radius,
                                         zarr_array* dst,
                                         const int64_t* dst_start);

// ── Gradient Magnitude (Sobel) ──────────────────────────────────────────────

// Compute 3D Sobel gradient magnitude over [start, stop).
// Output is written to dst at dst_start. Only supports 3D.
// Interior voxels get the gradient magnitude; boundary voxels get 0.
ZARR_NODISCARD zarr_status zarr_gradient_magnitude(const zarr_array* src,
                                                   const int64_t* start,
                                                   const int64_t* stop,
                                                   zarr_array* dst,
                                                   const int64_t* dst_start);

// ── Min Projection ──────────────────────────────────────────────────────────

// Min intensity projection along an axis over [start_idx, stop_idx).
ZARR_NODISCARD zarr_status zarr_project_min(const zarr_array* arr,
                                            zarr_axis axis,
                                            int64_t start_idx, int64_t stop_idx,
                                            uint8_t* buf, int64_t bufsize);

// ── Occupancy ───────────────────────────────────────────────────────────────

// Fraction of nonzero voxels in [start, stop). Returns value in [0, 1].
ZARR_NODISCARD zarr_status zarr_occupancy(const zarr_array* arr,
                                          const int64_t* start,
                                          const int64_t* stop,
                                          double* out);

// ── Utilities ───────────────────────────────────────────────────────────────

const char* zarr_status_str(zarr_status s);
const char* zarr_version_str(void);

// Print array info to FILE (use stdout for console).
void zarr_print_info(const zarr_array* arr, FILE* out);

#endif // ZARR_H
