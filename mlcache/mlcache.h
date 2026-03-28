// mlcache.h — Pure C23 multilevel cache library for volumetric data
// Vesuvius Challenge / Villa Volume Cartographer.
//
// 4-tier cache: HOT (decoded mini-cubes in RAM) → WARM (compressed chunks in RAM)
//             → COLD (zarr on disk) → ICE (S3 remote via libs3)
//
// HOT tier uses 16^3 mini-cubes (4KB page-aligned) as eviction unit.
// WARM tier uses 128^3 chunks (VL264/H264 compressed) as eviction unit.
//
// Dependencies (all optional, conditional compilation):
//   MLC_COLD   — zarr.h for disk backend
//   MLC_ICE    — s3.h for S3 backend (implies MLC_COLD)
//   MLC_TENSOR — tensor.h for zero-copy tensor views
#ifndef MLCACHE_H
#define MLCACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ── Version ─────────────────────────────────────────────────────────────────

#define MLC_VERSION_MAJOR 0
#define MLC_VERSION_MINOR 1
#define MLC_VERSION_PATCH 0

// ── C23 Compat ──────────────────────────────────────────────────────────────

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define MLC_NODISCARD    [[nodiscard]]
  #define MLC_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define MLC_NODISCARD    __attribute__((warn_unused_result))
  #define MLC_MAYBE_UNUSED __attribute__((unused))
#else
  #define MLC_NODISCARD
  #define MLC_MAYBE_UNUSED
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define MLC_HOT    __attribute__((hot))
  #define MLC_COLD_A __attribute__((cold))
#else
  #define MLC_HOT
  #define MLC_COLD_A
#endif

// ── Linkage ─────────────────────────────────────────────────────────────────

#ifndef MLCDEF
  #ifdef MLC_STATIC
    #define MLCDEF static
  #else
    #define MLCDEF extern
  #endif
#endif

// ── Allocator Hooks ─────────────────────────────────────────────────────────

#ifndef MLC_MALLOC
  #include <stdlib.h>
  #define MLC_MALLOC(sz)     malloc(sz)
  #define MLC_REALLOC(p, sz) realloc(p, sz)
  #define MLC_FREE(p)        free(p)
  #define MLC_CALLOC(n, sz)  calloc(n, sz)
#endif

// ── Constants ───────────────────────────────────────────────────────────────

#define MLC_CHUNK_DIM          128
#define MLC_CHUNK_VOX          (MLC_CHUNK_DIM * MLC_CHUNK_DIM * MLC_CHUNK_DIM) // 2,097,152
#define MLC_MINI_DIM           16
#define MLC_MINI_VOX           (MLC_MINI_DIM * MLC_MINI_DIM * MLC_MINI_DIM)   // 4,096
#define MLC_MINIS_PER_AXIS     (MLC_CHUNK_DIM / MLC_MINI_DIM)                 // 8
#define MLC_MINIS_PER_CHUNK    (MLC_MINIS_PER_AXIS * MLC_MINIS_PER_AXIS * MLC_MINIS_PER_AXIS) // 512
#define MLC_SHARD_AXIS         8
#define MLC_SHARD_CHUNKS       (MLC_SHARD_AXIS * MLC_SHARD_AXIS * MLC_SHARD_AXIS) // 512
#define MLC_MAX_LOD            16
#define MLC_MAX_SUBSCRIBERS    64
#define MLC_PAGE_SIZE          4096

// ── Status Codes ────────────────────────────────────────────────────────────

typedef enum mlc_status {
    MLC_OK = 0,
    MLC_ERR_NULL_ARG,
    MLC_ERR_ALLOC,
    MLC_ERR_INVALID_CONFIG,
    MLC_ERR_NOT_FOUND,
    MLC_ERR_IO,
    MLC_ERR_NETWORK,
    MLC_ERR_CODEC,
    MLC_ERR_BOUNDS,
    MLC_ERR_FULL,
    MLC_ERR_SHUTDOWN,
    MLC_ERR_TIMEOUT,
    MLC_ERR_UNSUPPORTED,
} mlc_status;

// ── Tier Enum ───────────────────────────────────────────────────────────────

typedef enum mlc_tier {
    MLC_TIER_HOT = 0,   // decoded 16^3 mini-cubes in RAM (4KB each)
    MLC_TIER_WARM,       // compressed 128^3 chunks in RAM (VL264, ~250KB-2MB)
    MLC_TIER_COLD,       // zarr arrays on local disk
    MLC_TIER_ICE,        // S3 remote storage via libs3
    MLC_TIER_COUNT,
} mlc_tier;

// ── Priority Enum ───────────────────────────────────────────────────────────

typedef enum mlc_priority {
    MLC_PRIORITY_CRITICAL = 0, // user staring at this voxel right now
    MLC_PRIORITY_HIGH,         // adjacent to viewport
    MLC_PRIORITY_NORMAL,       // prefetch ring 1
    MLC_PRIORITY_LOW,          // prefetch ring 2
    MLC_PRIORITY_BACKGROUND,   // speculative
} mlc_priority;

// ── LOD Quality ─────────────────────────────────────────────────────────────

typedef enum mlc_lod_quality {
    MLC_QP_LOSSLESS      = 0,
    MLC_QP_NEAR_LOSSLESS = 4,
    MLC_QP_HIGH          = 10,
    MLC_QP_BALANCED      = 18,
} mlc_lod_quality;

// ── Event Types ─────────────────────────────────────────────────────────────

typedef enum mlc_event {
    MLC_EVENT_MINI_READY = 0,   // a mini-cube arrived in HOT tier
    MLC_EVENT_CHUNK_DECODED,    // a full chunk was decoded (all 512 minis available)
    MLC_EVENT_LOD_UPGRADE,      // higher-resolution data replaced lower-res
    MLC_EVENT_EVICTION,         // data was evicted from a tier
    MLC_EVENT_PRESSURE,         // tier under memory pressure (>95% full)
    MLC_EVENT_STATS_UPDATE,     // periodic stats snapshot available
    MLC_EVENT_COUNT,
} mlc_event;

// ── Key Types ───────────────────────────────────────────────────────────────
// Concrete structs for stack allocation and hashing.

typedef struct mlc_chunk_key {
    int32_t lod;           // LOD level (0 = full resolution)
    int32_t cx, cy, cz;   // chunk coordinates (units of 128 voxels)
} mlc_chunk_key;

typedef struct mlc_mini_key {
    int32_t lod;
    int32_t mx, my, mz;   // mini-cube coordinates (units of 16 voxels, global)
} mlc_mini_key;

typedef struct mlc_region {
    int32_t lod;
    int32_t x0, y0, z0;   // inclusive start (voxel coords)
    int32_t x1, y1, z1;   // exclusive end (voxel coords)
} mlc_region;

// ── Configuration ───────────────────────────────────────────────────────────

typedef struct mlc_volume_info {
    int32_t shape[3];          // full-resolution volume shape [z, y, x]
    int32_t lod_levels;        // number of LOD levels (1 = no pyramid)
    int32_t downsample_factor; // typically 2
    int32_t qp;                // VL264 quality preset (mlc_lod_quality)
} mlc_volume_info;

typedef struct mlc_tier_config {
    size_t   hot_budget;         // bytes, default 2GB
    size_t   warm_budget;        // bytes, default 512MB
    char     cold_root[1024];    // path to zarr directory on disk
    char     ice_bucket[256];    // S3 bucket name
    char     ice_prefix[512];    // S3 key prefix
#if defined(MLC_ICE)
    void    *ice_s3_config;      // pointer to s3_config, NULL = no ICE tier
#else
    void    *ice_s3_config;      // unused without MLC_ICE, always NULL
#endif
} mlc_tier_config;

typedef struct mlc_thread_config {
    int32_t decode_workers;  // threads for WARM→HOT decode (default = ncpu/2)
    int32_t io_workers;      // threads for COLD→WARM disk reads (default = 4)
    int32_t net_workers;     // threads for ICE→COLD network fetches (default = 4)
} mlc_thread_config;

typedef struct mlc_config {
    mlc_volume_info   volume;
    mlc_tier_config   tiers;
    mlc_thread_config threads;
    bool              adaptive_budgets;   // enable adaptive tier budget controller
    int32_t           stats_interval_ms;  // stats callback period, 0 = disabled
} mlc_config;

// ── Codec Callback Types ────────────────────────────────────────────────────

// Decode callback: decompress a chunk from compressed bytes to decoded 128^3 u8.
// compressed: input compressed bytes, compressed_len: input length.
// decoded_out: pre-allocated MLC_CHUNK_VOX (2MB) buffer to write decoded data.
// Return MLC_OK on success.
typedef mlc_status (*mlc_decode_fn)(const void *compressed, size_t compressed_len,
                                     uint8_t *decoded_out, void *userdata);

// Encode callback: compress a decoded 128^3 u8 chunk.
// decoded: input MLC_CHUNK_VOX bytes.
// compressed_out: set to malloc'd buffer (caller frees).
// compressed_len_out: set to output length.
typedef mlc_status (*mlc_encode_fn)(const uint8_t *decoded,
                                     void **compressed_out,
                                     size_t *compressed_len_out,
                                     void *userdata);

// ── Event Callback Types ────────────────────────────────────────────────────

// Event callback. Return 0 to stay subscribed, non-zero to unsubscribe.
typedef int  (*mlc_event_fn)(mlc_event event, const void *detail, void *userdata);

// Mini-cube ready callback (called on worker thread — consumer must be thread-safe).
typedef void (*mlc_mini_ready_fn)(mlc_mini_key key, const uint8_t *data, void *userdata);

// Chunk ready callback (all 512 minis of a chunk decoded).
typedef void (*mlc_chunk_ready_fn)(mlc_chunk_key key, const uint8_t *data, void *userdata);

// ── Statistics ──────────────────────────────────────────────────────────────

typedef struct mlc_tier_stats {
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t bytes_used;
    uint64_t bytes_budget;
    uint64_t items;
    double   hit_rate;        // hits / (hits + misses), 0.0 if no accesses
    double   avg_latency_us;  // average access latency in microseconds
    double   p99_latency_us;  // 99th percentile latency
} mlc_tier_stats;

typedef struct mlc_stats {
    mlc_tier_stats tier[MLC_TIER_COUNT];
    uint64_t       pending_fetches;
    uint64_t       deduplicated_requests;
    uint64_t       prefetch_hits;
    uint64_t       total_requests;
    double         overall_hit_rate;
} mlc_stats;

// ── Event Detail Structs ────────────────────────────────────────────────────

typedef struct mlc_event_mini_ready {
    mlc_mini_key   key;
    const uint8_t *data;
} mlc_event_mini_ready;

typedef struct mlc_event_chunk_decoded {
    mlc_chunk_key  key;
} mlc_event_chunk_decoded;

typedef struct mlc_event_lod_upgrade {
    mlc_mini_key   key;
    int32_t        old_lod;
    int32_t        new_lod;
} mlc_event_lod_upgrade;

typedef struct mlc_event_eviction {
    mlc_tier tier;
    uint64_t items_evicted;
    uint64_t bytes_freed;
} mlc_event_eviction;

typedef struct mlc_event_pressure {
    mlc_tier tier;
    double   utilization; // bytes_used / budget (0.0 to 1.0+)
} mlc_event_pressure;

// ── Opaque Handle ───────────────────────────────────────────────────────────

typedef struct mlc_cache mlc_cache;

// ── Lifecycle ───────────────────────────────────────────────────────────────

// Return a config with sane defaults (2GB HOT, 512MB WARM, ncpu/2 decode workers).
MLCDEF mlc_config mlc_default_config(void);

// Create a cache instance. Config is copied internally.
MLC_NODISCARD MLCDEF mlc_status mlc_create(mlc_cache **out, const mlc_config *config);

// Destroy: drains queues, joins threads, frees all memory.
MLCDEF void mlc_destroy(mlc_cache *c);

// ── Synchronous Data Access (pin-based lifetime) ────────────────────────────
// Returned pointers are pinned (refcounted); call mlc_unpin_* when done.
// Pinned data will not be evicted.

// Get a 16^3 mini-cube (4KB). Blocking: fetches through tiers if not in HOT.
MLC_NODISCARD MLC_HOT MLCDEF mlc_status mlc_get_mini(
    mlc_cache *c, mlc_mini_key key, const uint8_t **data_out);

// Release pin on a mini-cube, allowing eviction.
MLCDEF void mlc_unpin_mini(mlc_cache *c, mlc_mini_key key);

// Get a 128^3 chunk (2MB decoded). Blocking.
MLC_NODISCARD MLCDEF mlc_status mlc_get_chunk(
    mlc_cache *c, mlc_chunk_key key, const uint8_t **data_out);

// Release pin on a chunk.
MLCDEF void mlc_unpin_chunk(mlc_cache *c, mlc_chunk_key key);

// ── Asynchronous Data Access ────────────────────────────────────────────────
// Non-blocking: enqueue for background fetch, call callback when ready.

MLC_NODISCARD MLCDEF mlc_status mlc_request_mini(
    mlc_cache *c, mlc_mini_key key, mlc_priority priority,
    mlc_mini_ready_fn callback, void *userdata);

MLC_NODISCARD MLCDEF mlc_status mlc_request_chunk(
    mlc_cache *c, mlc_chunk_key key, mlc_priority priority,
    mlc_chunk_ready_fn callback, void *userdata);

// ── Region Reads ────────────────────────────────────────────────────────────

// Read an arbitrary sub-region into a caller-supplied buffer.
// Assembles from multiple mini-cubes. Blocking.
MLC_NODISCARD MLCDEF mlc_status mlc_read_region(
    mlc_cache *c, mlc_region region, uint8_t *buf, size_t bufsize);

// ── Prefetch ────────────────────────────────────────────────────────────────

// Prefetch a region at given priority. Non-blocking.
MLC_NODISCARD MLCDEF mlc_status mlc_prefetch_region(
    mlc_cache *c, mlc_region region, mlc_priority priority);

// Prefetch around a viewpoint in concentric shells with decreasing priority.
MLC_NODISCARD MLCDEF mlc_status mlc_prefetch_around(
    mlc_cache *c, int32_t lod, int32_t vx, int32_t vy, int32_t vz,
    int32_t radius_chunks);

// Cancel all pending prefetch requests at or below given priority.
MLCDEF void mlc_cancel_prefetch(mlc_cache *c, mlc_priority min_priority);

// Set current camera position for adaptive prefetch heuristics.
// The prefetch engine uses this + velocity estimation for prediction.
MLCDEF void mlc_set_viewpoint(mlc_cache *c, int32_t lod,
    float vx, float vy, float vz);

// ── Progressive Rendering ───────────────────────────────────────────────────

// Request data for a region with LOD fallback: delivers coarsest available
// LOD first, then fires MLC_EVENT_LOD_UPGRADE when finer data arrives.
MLC_NODISCARD MLCDEF mlc_status mlc_progressive_request(
    mlc_cache *c, mlc_region region,
    mlc_mini_ready_fn callback, void *userdata);

// ── Event Subscription ─────────────────────────────────────────────────────

// Subscribe to cache events. Returns subscription ID via sub_id_out.
MLC_NODISCARD MLCDEF mlc_status mlc_subscribe(
    mlc_cache *c, mlc_event event, mlc_event_fn fn, void *userdata,
    uint64_t *sub_id_out);

// Unsubscribe from events.
MLCDEF void mlc_unsubscribe(mlc_cache *c, uint64_t sub_id);

// ── Statistics & Diagnostics ────────────────────────────────────────────────

// Snapshot current stats. Thread-safe, lock-free read of atomic counters.
MLCDEF mlc_status mlc_get_stats(const mlc_cache *c, mlc_stats *out);

// Per-tier stats.
MLCDEF mlc_status mlc_get_tier_stats(const mlc_cache *c, mlc_tier tier,
    mlc_tier_stats *out);

// Reset all stats counters.
MLCDEF void mlc_reset_stats(mlc_cache *c);

// ── Budget Control ──────────────────────────────────────────────────────────

// Resize a tier budget at runtime. Triggers eviction if shrinking.
MLCDEF mlc_status mlc_set_budget(mlc_cache *c, mlc_tier tier, size_t bytes);

// Get current budget for a tier.
MLCDEF size_t mlc_get_budget(const mlc_cache *c, mlc_tier tier);

// Force eviction of a fraction (0.0 to 1.0) from a tier.
MLCDEF mlc_status mlc_evict(mlc_cache *c, mlc_tier tier, float fraction);

// Flush all tiers (e.g., before volume switch).
MLCDEF void mlc_flush(mlc_cache *c);

// ── Coordinate Utilities ────────────────────────────────────────────────────

// Convert mini key to its containing chunk key.
MLCDEF mlc_chunk_key mlc_mini_to_chunk(mlc_mini_key mk);

// Convert voxel coordinates to mini key.
MLCDEF mlc_mini_key mlc_voxel_to_mini(int32_t lod, int32_t x, int32_t y, int32_t z);

// Convert voxel coordinates to chunk key.
MLCDEF mlc_chunk_key mlc_voxel_to_chunk(int32_t lod, int32_t x, int32_t y, int32_t z);

// Hash functions (useful for consumers building their own tables).
MLCDEF uint64_t mlc_chunk_key_hash(mlc_chunk_key k);
MLCDEF uint64_t mlc_mini_key_hash(mlc_mini_key k);

// Key equality.
MLCDEF bool mlc_chunk_key_eq(mlc_chunk_key a, mlc_chunk_key b);
MLCDEF bool mlc_mini_key_eq(mlc_mini_key a, mlc_mini_key b);

// ── Codec Registration ──────────────────────────────────────────────────────

// Set the decoder callback (required for WARM→HOT promotion).
// Without a decoder, compressed data in WARM cannot be used.
MLCDEF void mlc_set_decoder(mlc_cache *c, mlc_decode_fn fn, void *userdata);

// Set the encoder callback (optional, for HOT→WARM demotion).
MLCDEF void mlc_set_encoder(mlc_cache *c, mlc_encode_fn fn, void *userdata);

// ── Data Injection ──────────────────────────────────────────────────────────

// Inject a decoded 128^3 chunk directly into HOT tier (all 512 minis).
// The cache copies the data. Useful for testing and direct data loading.
MLC_NODISCARD MLCDEF mlc_status mlc_inject_chunk(
    mlc_cache *c, mlc_chunk_key key, const uint8_t *decoded_data);

// Inject compressed chunk data into WARM tier.
// The cache copies the data.
MLC_NODISCARD MLCDEF mlc_status mlc_inject_compressed(
    mlc_cache *c, mlc_chunk_key key, const void *compressed, size_t len);

// ── Scroll Hint ─────────────────────────────────────────────────────────────

// Hint that the user is scrolling slices along an axis.
// axis: 0=z, 1=y, 2=x. velocity: slices/second (+ = increasing index).
MLCDEF void mlc_hint_scroll(mlc_cache *c, int32_t lod,
    int axis, float position, float velocity);

// ── String Utilities ────────────────────────────────────────────────────────

MLCDEF const char *mlc_status_str(mlc_status s);
MLCDEF const char *mlc_tier_str(mlc_tier t);
MLCDEF const char *mlc_version_str(void);

// ── Tensor Integration (conditional MLC_TENSOR) ─────────────────────────────

#if defined(MLC_TENSOR) && defined(TENSOR_H)

// Get a zero-copy tensor view of a cached mini-cube.
// Returns ts_tensor* wrapping pinned data (shape [16,16,16], TS_U8).
// Caller must ts_free() the tensor AND mlc_unpin_mini() the key.
MLC_NODISCARD MLCDEF mlc_status mlc_get_mini_tensor(
    mlc_cache *c, mlc_mini_key key, ts_tensor **tensor_out);

#endif // MLC_TENSOR

#endif // MLCACHE_H
