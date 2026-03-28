// mlcache.c — Implementation of the multilevel cache library.
// Phase 1: Core data structures, HOT/WARM tiers, CLOCK eviction, lifecycle.
// Phase 2: Thread pools, fetch engine, priority queue, deduplication.

#include "mlcache.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// ── Internal Constants ──────────────────────────────────────────────────────

#define MLC_HOT_HIGH_WATER  0.95
#define MLC_HOT_LOW_WATER   0.85
#define MLC_WARM_HIGH_WATER 0.95
#define MLC_WARM_LOW_WATER  0.85

// Robin-hood hash table load factor threshold for resize.
#define MLC_HT_MAX_LOAD     0.75
#define MLC_HT_INIT_CAP     1024
// Max probe distance before we resize.
#define MLC_HT_MAX_PROBE    128

// Budget controller limits.
#define MLC_MIN_HOT_BUDGET  (256ULL << 20)  // 256 MB
#define MLC_MIN_WARM_BUDGET (64ULL << 20)   // 64 MB

// ── Platform Helpers ────────────────────────────────────────────────────────

static inline uint64_t mlc__now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static inline int mlc__ncpu(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

static inline void *mlc__alloc_aligned(size_t alignment, size_t size) {
    void *p = NULL;
    if (posix_memalign(&p, alignment, size) != 0) return NULL;
    return p;
}

// ── FNV-1a Mixing ───────────────────────────────────────────────────────────

static inline uint64_t mlc__mix(uint64_t h, uint64_t v) {
    h ^= v * 2654435761ULL;
    h *= 1099511628211ULL;
    return h;
}

// ═══════════════════════════════════════════════════════════════════════════
// Robin-Hood Hash Table (generic via macros)
// ═══════════════════════════════════════════════════════════════════════════
//
// Each table is a flat array of slots. Each slot has:
//   - occupied: whether the slot holds a valid entry
//   - psl: probe sequence length (distance from ideal position)
//   - key: the key
//   - value: the value
//
// We define separate tables for HOT (mini_key -> hot_entry) and
// WARM (chunk_key -> warm_entry) using concrete structs rather than
// macro soup, for debuggability.

// ── HOT Entry ───────────────────────────────────────────────────────────────

typedef struct mlc__hot_entry {
    uint8_t         *data;         // 4KB page-aligned mini-cube
    _Atomic uint64_t last_access;  // clock tick
    _Atomic int32_t  pin_count;    // >0 = cannot evict
    uint32_t         flags;        // reserved
} mlc__hot_entry;

typedef struct mlc__hot_slot {
    bool            occupied;
    uint16_t        psl;         // probe sequence length
    mlc_mini_key    key;
    mlc__hot_entry  val;
} mlc__hot_slot;

typedef struct mlc__hot_table {
    mlc__hot_slot  *slots;
    uint32_t        cap;       // always power of 2
    uint32_t        count;
    uint32_t        mask;      // cap - 1
} mlc__hot_table;

// ── WARM Entry ──────────────────────────────────────────────────────────────

typedef struct mlc__warm_entry {
    uint8_t         *h264_data;    // compressed bytes
    uint32_t         h264_len;
    _Atomic uint64_t last_access;
    _Atomic int32_t  pin_count;
} mlc__warm_entry;

typedef struct mlc__warm_slot {
    bool              occupied;
    uint16_t          psl;
    mlc_chunk_key     key;
    mlc__warm_entry   val;
} mlc__warm_slot;

typedef struct mlc__warm_table {
    mlc__warm_slot *slots;
    uint32_t        cap;
    uint32_t        count;
    uint32_t        mask;
} mlc__warm_table;

// ── Priority Queue (binary min-heap) ────────────────────────────────────────

typedef struct mlc__fetch_req {
    mlc_chunk_key  key;
    mlc_priority   priority;
    float          distance_sq;   // squared distance to viewpoint
    uint64_t       timestamp;     // insertion time for FIFO within same priority
} mlc__fetch_req;

typedef struct mlc__pqueue {
    mlc__fetch_req *heap;
    uint32_t        count;
    uint32_t        cap;
} mlc__pqueue;

// ── Request Deduplication (inflight set) ────────────────────────────────────

typedef struct mlc__waiter {
    mlc_chunk_ready_fn chunk_cb;
    mlc_mini_ready_fn  mini_cb;
    void              *userdata;
    struct mlc__waiter *next;
} mlc__waiter;

typedef struct mlc__inflight_entry {
    mlc_chunk_key             key;
    bool                      occupied;
    mlc__waiter              *waiters;
} mlc__inflight_entry;

typedef struct mlc__inflight_table {
    mlc__inflight_entry *entries;
    uint32_t             cap;
    uint32_t             count;
    uint32_t             mask;
} mlc__inflight_table;

// ── Fetch Engine ────────────────────────────────────────────────────────────

typedef struct mlc__fetch_engine {
    mlc__pqueue          pq;
    mlc__inflight_table  inflight;
    pthread_mutex_t      mu;
    pthread_cond_t       cv;
    pthread_t           *decode_threads;
    pthread_t           *io_threads;
    int32_t              n_decode;
    int32_t              n_io;
    _Atomic bool         stop;
    uint64_t             next_ts; // monotonic insertion counter
} mlc__fetch_engine;

// ── Prefetch State ──────────────────────────────────────────────────────────

typedef struct mlc__prefetch_state {
    float    viewpoint[3];
    float    velocity[3];
    float    prev_viewpoint[3];
    uint64_t prev_time_us;
    int32_t  current_lod;
} mlc__prefetch_state;

// ── Event System ────────────────────────────────────────────────────────────

typedef struct mlc__subscriber {
    mlc_event_fn fn;
    void        *userdata;
    uint64_t     id;
    bool         active;
} mlc__subscriber;

typedef struct mlc__event_system {
    mlc__subscriber    subs[MLC_EVENT_COUNT][MLC_MAX_SUBSCRIBERS];
    _Atomic uint64_t   next_id;
    pthread_rwlock_t    lock;
} mlc__event_system;

// ── Atomic Stats Counters ───────────────────────────────────────────────────

typedef struct mlc__counters {
    _Atomic uint64_t hits[MLC_TIER_COUNT];
    _Atomic uint64_t misses[MLC_TIER_COUNT];
    _Atomic uint64_t evictions[MLC_TIER_COUNT];
    _Atomic uint64_t dedup_count;
    _Atomic uint64_t prefetch_issued;
    _Atomic uint64_t prefetch_used;
    _Atomic uint64_t total_requests;
} mlc__counters;

// Latency tracker: log2-spaced buckets from 1us to ~10s (24 buckets).
#define MLC_LATENCY_BUCKETS 24
typedef struct mlc__latency_tracker {
    _Atomic uint64_t buckets[MLC_LATENCY_BUCKETS];
    _Atomic uint64_t count;
    _Atomic uint64_t sum_us;
} mlc__latency_tracker;

// ── Budget Controller ───────────────────────────────────────────────────────

typedef struct mlc__budget_ctrl {
    double hot_hit_ema;
    double warm_hit_ema;
    double alpha;           // EMA smoothing (0.1)
    size_t total_ram;       // hot + warm (conserved)
    bool   enabled;
} mlc__budget_ctrl;

// ═══════════════════════════════════════════════════════════════════════════
// Main Cache Struct
// ═══════════════════════════════════════════════════════════════════════════

struct mlc_cache {
    mlc_config config;

    // HOT tier
    mlc__hot_table      hot;
    pthread_rwlock_t    hot_lock;
    _Atomic size_t      hot_bytes;
    size_t              hot_budget;
    uint32_t            hot_clock_hand;

    // WARM tier
    mlc__warm_table     warm;
    pthread_rwlock_t    warm_lock;
    _Atomic size_t      warm_bytes;
    size_t              warm_budget;
    uint32_t            warm_clock_hand;

    // Global monotonic clock
    _Atomic uint64_t    clock;

    // Fetch engine
    mlc__fetch_engine   fetch;

    // Prefetch state
    mlc__prefetch_state prefetch;
    pthread_mutex_t     prefetch_lock;

    // Budget controller
    mlc__budget_ctrl    budget;

    // Event system
    mlc__event_system   events;

    // Statistics
    mlc__counters       counters;
    mlc__latency_tracker latency[MLC_TIER_COUNT];

    // Codec callbacks
    mlc_decode_fn       decode_fn;
    void               *decode_userdata;
    mlc_encode_fn       encode_fn;
    void               *encode_userdata;

    // Scroll hint state
    struct {
        int32_t lod;
        int     axis;       // 0=z, 1=y, 2=x
        float   position;
        float   velocity;   // slices/sec
    } scroll_hint;

    // Shutdown flag
    _Atomic bool        shutting_down;
};

// ═══════════════════════════════════════════════════════════════════════════
// Hash / Equality Helpers
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF uint64_t mlc_chunk_key_hash(mlc_chunk_key k) {
    uint64_t h = 14695981039346656037ULL; // FNV offset basis
    h = mlc__mix(h, (uint64_t)(uint32_t)k.lod);
    h = mlc__mix(h, (uint64_t)(uint32_t)k.cx);
    h = mlc__mix(h, (uint64_t)(uint32_t)k.cy);
    h = mlc__mix(h, (uint64_t)(uint32_t)k.cz);
    return h;
}

MLCDEF uint64_t mlc_mini_key_hash(mlc_mini_key k) {
    uint64_t h = 14695981039346656037ULL;
    h = mlc__mix(h, (uint64_t)(uint32_t)k.lod);
    h = mlc__mix(h, (uint64_t)(uint32_t)k.mx);
    h = mlc__mix(h, (uint64_t)(uint32_t)k.my);
    h = mlc__mix(h, (uint64_t)(uint32_t)k.mz);
    return h;
}

MLCDEF bool mlc_chunk_key_eq(mlc_chunk_key a, mlc_chunk_key b) {
    return a.lod == b.lod && a.cx == b.cx && a.cy == b.cy && a.cz == b.cz;
}

MLCDEF bool mlc_mini_key_eq(mlc_mini_key a, mlc_mini_key b) {
    return a.lod == b.lod && a.mx == b.mx && a.my == b.my && a.mz == b.mz;
}

// ═══════════════════════════════════════════════════════════════════════════
// Coordinate Utilities
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_chunk_key mlc_mini_to_chunk(mlc_mini_key mk) {
    return (mlc_chunk_key){
        .lod = mk.lod,
        .cx = mk.mx / MLC_MINIS_PER_AXIS,
        .cy = mk.my / MLC_MINIS_PER_AXIS,
        .cz = mk.mz / MLC_MINIS_PER_AXIS,
    };
}

MLCDEF mlc_mini_key mlc_voxel_to_mini(int32_t lod, int32_t x, int32_t y, int32_t z) {
    return (mlc_mini_key){
        .lod = lod,
        .mx = x / MLC_MINI_DIM,
        .my = y / MLC_MINI_DIM,
        .mz = z / MLC_MINI_DIM,
    };
}

MLCDEF mlc_chunk_key mlc_voxel_to_chunk(int32_t lod, int32_t x, int32_t y, int32_t z) {
    return (mlc_chunk_key){
        .lod = lod,
        .cx = x / MLC_CHUNK_DIM,
        .cy = y / MLC_CHUNK_DIM,
        .cz = z / MLC_CHUNK_DIM,
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// String Utilities
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF const char *mlc_status_str(mlc_status s) {
    switch (s) {
    case MLC_OK:                 return "ok";
    case MLC_ERR_NULL_ARG:       return "null argument";
    case MLC_ERR_ALLOC:          return "allocation failed";
    case MLC_ERR_INVALID_CONFIG: return "invalid configuration";
    case MLC_ERR_NOT_FOUND:      return "not found";
    case MLC_ERR_IO:             return "I/O error";
    case MLC_ERR_NETWORK:        return "network error";
    case MLC_ERR_CODEC:          return "codec error";
    case MLC_ERR_BOUNDS:         return "out of bounds";
    case MLC_ERR_FULL:           return "cache full";
    case MLC_ERR_SHUTDOWN:       return "shutting down";
    case MLC_ERR_TIMEOUT:        return "timeout";
    case MLC_ERR_UNSUPPORTED:    return "unsupported";
    default:                     return "unknown";
    }
}

MLCDEF const char *mlc_tier_str(mlc_tier t) {
    switch (t) {
    case MLC_TIER_HOT:  return "HOT";
    case MLC_TIER_WARM: return "WARM";
    case MLC_TIER_COLD: return "COLD";
    case MLC_TIER_ICE:  return "ICE";
    default:            return "unknown";
    }
}

MLCDEF const char *mlc_version_str(void) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d",
             MLC_VERSION_MAJOR, MLC_VERSION_MINOR, MLC_VERSION_PATCH);
    return buf;
}

// ═══════════════════════════════════════════════════════════════════════════
// HOT Table (Robin-Hood Open Addressing)
// ═══════════════════════════════════════════════════════════════════════════

static inline uint32_t mlc__next_pow2(uint32_t v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4;
    v |= v >> 8; v |= v >> 16;
    return v + 1;
}

static mlc_status mlc__hot_init(mlc__hot_table *t, uint32_t init_cap) {
    uint32_t cap = mlc__next_pow2(init_cap < MLC_HT_INIT_CAP ? MLC_HT_INIT_CAP : init_cap);
    t->slots = (mlc__hot_slot *)MLC_CALLOC(cap, sizeof(mlc__hot_slot));
    if (!t->slots) return MLC_ERR_ALLOC;
    t->cap = cap;
    t->count = 0;
    t->mask = cap - 1;
    return MLC_OK;
}

static void mlc__hot_free(mlc__hot_table *t) {
    if (!t->slots) return;
    for (uint32_t i = 0; i < t->cap; i++) {
        if (t->slots[i].occupied && t->slots[i].val.data) {
            MLC_FREE(t->slots[i].val.data);
        }
    }
    MLC_FREE(t->slots);
    t->slots = NULL;
    t->cap = t->count = 0;
}

// Lookup: returns pointer to entry or NULL. Does NOT acquire lock.
static mlc__hot_entry *mlc__hot_find(mlc__hot_table *t, mlc_mini_key key) {
    uint64_t h = mlc_mini_key_hash(key);
    uint32_t idx = (uint32_t)(h & t->mask);
    uint16_t psl = 0;

    while (psl < MLC_HT_MAX_PROBE) {
        mlc__hot_slot *s = &t->slots[idx];
        if (!s->occupied) return NULL;
        if (s->psl < psl) return NULL; // robin-hood invariant: would have been placed here
        if (mlc_mini_key_eq(s->key, key)) return &s->val;
        idx = (idx + 1) & t->mask;
        psl++;
    }
    return NULL;
}

// Forward-declare resize.
static mlc_status mlc__hot_resize(mlc__hot_table *t, uint32_t new_cap);

// Insert (robin-hood). Caller must hold write lock.
static mlc_status mlc__hot_insert(mlc__hot_table *t, mlc_mini_key key, mlc__hot_entry val) {
    // Resize if load factor too high.
    if ((uint64_t)t->count * 100 >= (uint64_t)t->cap * (uint64_t)(MLC_HT_MAX_LOAD * 100)) {
        mlc_status s = mlc__hot_resize(t, t->cap * 2);
        if (s != MLC_OK) return s;
    }

    uint64_t h = mlc_mini_key_hash(key);
    uint32_t idx = (uint32_t)(h & t->mask);
    uint16_t psl = 0;

    mlc__hot_slot incoming = {
        .occupied = true,
        .psl = 0,
        .key = key,
        .val = val,
    };

    while (psl < MLC_HT_MAX_PROBE) {
        mlc__hot_slot *s = &t->slots[idx];
        if (!s->occupied) {
            *s = incoming;
            s->psl = psl;
            t->count++;
            return MLC_OK;
        }
        // Robin-hood: if existing entry has shorter PSL, swap and continue.
        if (s->psl < psl) {
            mlc__hot_slot tmp = *s;
            *s = incoming;
            s->psl = psl;
            incoming = tmp;
            psl = incoming.psl;
        }
        idx = (idx + 1) & t->mask;
        psl++;
        incoming.psl = psl;
    }

    // Should not happen with proper load factor.
    return MLC_ERR_FULL;
}

static mlc_status mlc__hot_resize(mlc__hot_table *t, uint32_t new_cap) {
    new_cap = mlc__next_pow2(new_cap);
    mlc__hot_slot *old_slots = t->slots;
    uint32_t old_cap = t->cap;

    t->slots = (mlc__hot_slot *)MLC_CALLOC(new_cap, sizeof(mlc__hot_slot));
    if (!t->slots) { t->slots = old_slots; return MLC_ERR_ALLOC; }
    t->cap = new_cap;
    t->mask = new_cap - 1;
    t->count = 0;

    for (uint32_t i = 0; i < old_cap; i++) {
        if (old_slots[i].occupied) {
            mlc__hot_insert(t, old_slots[i].key, old_slots[i].val);
        }
    }
    MLC_FREE(old_slots);
    return MLC_OK;
}

// Remove by key with backward-shift deletion. Returns true if found.
static bool mlc__hot_remove(mlc__hot_table *t, mlc_mini_key key) {
    uint64_t h = mlc_mini_key_hash(key);
    uint32_t idx = (uint32_t)(h & t->mask);
    uint16_t psl = 0;

    while (psl < MLC_HT_MAX_PROBE) {
        mlc__hot_slot *s = &t->slots[idx];
        if (!s->occupied) return false;
        if (s->psl < psl) return false;
        if (mlc_mini_key_eq(s->key, key)) {
            // Found it. Do backward-shift deletion.
            s->occupied = false;
            t->count--;

            // Shift subsequent entries backward to fill the gap.
            uint32_t prev = idx;
            uint32_t cur = (idx + 1) & t->mask;
            while (t->slots[cur].occupied && t->slots[cur].psl > 0) {
                t->slots[prev] = t->slots[cur];
                t->slots[prev].psl--;
                t->slots[cur].occupied = false;
                prev = cur;
                cur = (cur + 1) & t->mask;
            }
            return true;
        }
        idx = (idx + 1) & t->mask;
        psl++;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// WARM Table (same robin-hood pattern for chunk_key -> warm_entry)
// ═══════════════════════════════════════════════════════════════════════════

static mlc_status mlc__warm_init(mlc__warm_table *t, uint32_t init_cap) {
    uint32_t cap = mlc__next_pow2(init_cap < MLC_HT_INIT_CAP ? MLC_HT_INIT_CAP : init_cap);
    t->slots = (mlc__warm_slot *)MLC_CALLOC(cap, sizeof(mlc__warm_slot));
    if (!t->slots) return MLC_ERR_ALLOC;
    t->cap = cap;
    t->count = 0;
    t->mask = cap - 1;
    return MLC_OK;
}

static void mlc__warm_free(mlc__warm_table *t) {
    if (!t->slots) return;
    for (uint32_t i = 0; i < t->cap; i++) {
        if (t->slots[i].occupied && t->slots[i].val.h264_data) {
            MLC_FREE(t->slots[i].val.h264_data);
        }
    }
    MLC_FREE(t->slots);
    t->slots = NULL;
    t->cap = t->count = 0;
}

static mlc__warm_entry *mlc__warm_find(mlc__warm_table *t, mlc_chunk_key key) {
    uint64_t h = mlc_chunk_key_hash(key);
    uint32_t idx = (uint32_t)(h & t->mask);
    uint16_t psl = 0;

    while (psl < MLC_HT_MAX_PROBE) {
        mlc__warm_slot *s = &t->slots[idx];
        if (!s->occupied) return NULL;
        if (s->psl < psl) return NULL;
        if (mlc_chunk_key_eq(s->key, key)) return &s->val;
        idx = (idx + 1) & t->mask;
        psl++;
    }
    return NULL;
}

static mlc_status mlc__warm_resize(mlc__warm_table *t, uint32_t new_cap);

static mlc_status mlc__warm_insert(mlc__warm_table *t, mlc_chunk_key key, mlc__warm_entry val) {
    if ((uint64_t)t->count * 100 >= (uint64_t)t->cap * (uint64_t)(MLC_HT_MAX_LOAD * 100)) {
        mlc_status s = mlc__warm_resize(t, t->cap * 2);
        if (s != MLC_OK) return s;
    }

    uint64_t h = mlc_chunk_key_hash(key);
    uint32_t idx = (uint32_t)(h & t->mask);
    uint16_t psl = 0;

    mlc__warm_slot incoming = {
        .occupied = true,
        .psl = 0,
        .key = key,
        .val = val,
    };

    while (psl < MLC_HT_MAX_PROBE) {
        mlc__warm_slot *s = &t->slots[idx];
        if (!s->occupied) {
            *s = incoming;
            s->psl = psl;
            t->count++;
            return MLC_OK;
        }
        if (s->psl < psl) {
            mlc__warm_slot tmp = *s;
            *s = incoming;
            s->psl = psl;
            incoming = tmp;
            psl = incoming.psl;
        }
        idx = (idx + 1) & t->mask;
        psl++;
        incoming.psl = psl;
    }
    return MLC_ERR_FULL;
}

static mlc_status mlc__warm_resize(mlc__warm_table *t, uint32_t new_cap) {
    new_cap = mlc__next_pow2(new_cap);
    mlc__warm_slot *old_slots = t->slots;
    uint32_t old_cap = t->cap;

    t->slots = (mlc__warm_slot *)MLC_CALLOC(new_cap, sizeof(mlc__warm_slot));
    if (!t->slots) { t->slots = old_slots; return MLC_ERR_ALLOC; }
    t->cap = new_cap;
    t->mask = new_cap - 1;
    t->count = 0;

    for (uint32_t i = 0; i < old_cap; i++) {
        if (old_slots[i].occupied) {
            mlc__warm_insert(t, old_slots[i].key, old_slots[i].val);
        }
    }
    MLC_FREE(old_slots);
    return MLC_OK;
}

static bool mlc__warm_remove(mlc__warm_table *t, mlc_chunk_key key) {
    uint64_t h = mlc_chunk_key_hash(key);
    uint32_t idx = (uint32_t)(h & t->mask);
    uint16_t psl = 0;

    while (psl < MLC_HT_MAX_PROBE) {
        mlc__warm_slot *s = &t->slots[idx];
        if (!s->occupied) return false;
        if (s->psl < psl) return false;
        if (mlc_chunk_key_eq(s->key, key)) {
            s->occupied = false;
            t->count--;
            uint32_t prev = idx;
            uint32_t cur = (idx + 1) & t->mask;
            while (t->slots[cur].occupied && t->slots[cur].psl > 0) {
                t->slots[prev] = t->slots[cur];
                t->slots[prev].psl--;
                t->slots[cur].occupied = false;
                prev = cur;
                cur = (cur + 1) & t->mask;
            }
            return true;
        }
        idx = (idx + 1) & t->mask;
        psl++;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Priority Queue (binary min-heap)
// ═══════════════════════════════════════════════════════════════════════════

static mlc_status mlc__pq_init(mlc__pqueue *pq, uint32_t init_cap) {
    pq->heap = (mlc__fetch_req *)MLC_MALLOC(init_cap * sizeof(mlc__fetch_req));
    if (!pq->heap) return MLC_ERR_ALLOC;
    pq->count = 0;
    pq->cap = init_cap;
    return MLC_OK;
}

static void mlc__pq_free(mlc__pqueue *pq) {
    MLC_FREE(pq->heap);
    pq->heap = NULL;
    pq->count = pq->cap = 0;
}

// Comparison: lower is higher priority. priority first, then distance, then timestamp.
static inline bool mlc__pq_less(const mlc__fetch_req *a, const mlc__fetch_req *b) {
    if (a->priority != b->priority) return a->priority < b->priority;
    if (a->distance_sq != b->distance_sq) return a->distance_sq < b->distance_sq;
    return a->timestamp < b->timestamp;
}

static void mlc__pq_sift_up(mlc__pqueue *pq, uint32_t i) {
    while (i > 0) {
        uint32_t parent = (i - 1) / 2;
        if (mlc__pq_less(&pq->heap[i], &pq->heap[parent])) {
            mlc__fetch_req tmp = pq->heap[i];
            pq->heap[i] = pq->heap[parent];
            pq->heap[parent] = tmp;
            i = parent;
        } else break;
    }
}

static void mlc__pq_sift_down(mlc__pqueue *pq, uint32_t i) {
    while (true) {
        uint32_t best = i;
        uint32_t l = 2 * i + 1;
        uint32_t r = 2 * i + 2;
        if (l < pq->count && mlc__pq_less(&pq->heap[l], &pq->heap[best])) best = l;
        if (r < pq->count && mlc__pq_less(&pq->heap[r], &pq->heap[best])) best = r;
        if (best == i) break;
        mlc__fetch_req tmp = pq->heap[i];
        pq->heap[i] = pq->heap[best];
        pq->heap[best] = tmp;
        i = best;
    }
}

static mlc_status mlc__pq_push(mlc__pqueue *pq, mlc__fetch_req req) {
    if (pq->count >= pq->cap) {
        uint32_t new_cap = pq->cap ? pq->cap * 2 : 256;
        mlc__fetch_req *nh = (mlc__fetch_req *)MLC_REALLOC(pq->heap,
            new_cap * sizeof(mlc__fetch_req));
        if (!nh) return MLC_ERR_ALLOC;
        pq->heap = nh;
        pq->cap = new_cap;
    }
    pq->heap[pq->count] = req;
    mlc__pq_sift_up(pq, pq->count);
    pq->count++;
    return MLC_OK;
}

static bool mlc__pq_pop(mlc__pqueue *pq, mlc__fetch_req *out) {
    if (pq->count == 0) return false;
    *out = pq->heap[0];
    pq->count--;
    if (pq->count > 0) {
        pq->heap[0] = pq->heap[pq->count];
        mlc__pq_sift_down(pq, 0);
    }
    return true;
}

// Priority escalation: find key and upgrade priority if lower (better).
static void mlc__pq_escalate(mlc__pqueue *pq, mlc_chunk_key key, mlc_priority new_prio) {
    for (uint32_t i = 0; i < pq->count; i++) {
        if (mlc_chunk_key_eq(pq->heap[i].key, key) && new_prio < pq->heap[i].priority) {
            pq->heap[i].priority = new_prio;
            mlc__pq_sift_up(pq, i);
            return;
        }
    }
}

// Cancel all entries at or below (numerically >=) given priority.
static void mlc__pq_cancel_below(mlc__pqueue *pq, mlc_priority min_prio) {
    // Compact in-place: keep only entries with priority < min_prio.
    uint32_t write = 0;
    for (uint32_t read = 0; read < pq->count; read++) {
        if (pq->heap[read].priority < min_prio) {
            pq->heap[write++] = pq->heap[read];
        }
    }
    pq->count = write;
    // Re-heapify.
    for (uint32_t i = pq->count / 2; i > 0; i--) {
        mlc__pq_sift_down(pq, i - 1);
    }
    if (pq->count > 0) mlc__pq_sift_down(pq, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Inflight Deduplication Table
// ═══════════════════════════════════════════════════════════════════════════

static mlc_status mlc__inflight_init(mlc__inflight_table *t, uint32_t cap) {
    cap = mlc__next_pow2(cap < 256 ? 256 : cap);
    t->entries = (mlc__inflight_entry *)MLC_CALLOC(cap, sizeof(mlc__inflight_entry));
    if (!t->entries) return MLC_ERR_ALLOC;
    t->cap = cap;
    t->count = 0;
    t->mask = cap - 1;
    return MLC_OK;
}

static void mlc__inflight_free(mlc__inflight_table *t) {
    if (!t->entries) return;
    // Free any remaining waiter lists.
    for (uint32_t i = 0; i < t->cap; i++) {
        mlc__waiter *w = t->entries[i].waiters;
        while (w) {
            mlc__waiter *next = w->next;
            MLC_FREE(w);
            w = next;
        }
    }
    MLC_FREE(t->entries);
    t->entries = NULL;
}

// Check if key is inflight. Returns pointer to entry or NULL.
static mlc__inflight_entry *mlc__inflight_find(mlc__inflight_table *t, mlc_chunk_key key) {
    uint64_t h = mlc_chunk_key_hash(key);
    uint32_t idx = (uint32_t)(h & t->mask);
    for (uint32_t i = 0; i < 64; i++) { // linear probe with limited search
        mlc__inflight_entry *e = &t->entries[(idx + i) & t->mask];
        if (!e->occupied) return NULL;
        if (mlc_chunk_key_eq(e->key, key)) return e;
    }
    return NULL;
}

// Insert key into inflight table.
static mlc_status mlc__inflight_insert(mlc__inflight_table *t, mlc_chunk_key key) {
    uint64_t h = mlc_chunk_key_hash(key);
    uint32_t idx = (uint32_t)(h & t->mask);
    for (uint32_t i = 0; i < 64; i++) {
        mlc__inflight_entry *e = &t->entries[(idx + i) & t->mask];
        if (!e->occupied) {
            e->occupied = true;
            e->key = key;
            e->waiters = NULL;
            t->count++;
            return MLC_OK;
        }
    }
    return MLC_ERR_FULL;
}

// Remove key and return its waiter list.
static mlc__waiter *mlc__inflight_remove(mlc__inflight_table *t, mlc_chunk_key key) {
    uint64_t h = mlc_chunk_key_hash(key);
    uint32_t idx = (uint32_t)(h & t->mask);
    for (uint32_t i = 0; i < 64; i++) {
        mlc__inflight_entry *e = &t->entries[(idx + i) & t->mask];
        if (!e->occupied) return NULL;
        if (mlc_chunk_key_eq(e->key, key)) {
            mlc__waiter *w = e->waiters;
            e->occupied = false;
            e->waiters = NULL;
            t->count--;
            return w;
        }
    }
    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// Event System
// ═══════════════════════════════════════════════════════════════════════════

static void mlc__events_init(mlc__event_system *es) {
    memset(es->subs, 0, sizeof(es->subs));
    atomic_store(&es->next_id, 1);
    pthread_rwlock_init(&es->lock, NULL);
}

static void mlc__events_destroy(mlc__event_system *es) {
    pthread_rwlock_destroy(&es->lock);
}

static mlc_status mlc__events_subscribe(mlc__event_system *es, mlc_event event,
                                        mlc_event_fn fn, void *userdata,
                                        uint64_t *id_out) {
    if ((int)event < 0 || (int)event >= MLC_EVENT_COUNT) return MLC_ERR_INVALID_CONFIG;
    pthread_rwlock_wrlock(&es->lock);
    for (int i = 0; i < MLC_MAX_SUBSCRIBERS; i++) {
        if (!es->subs[event][i].active) {
            uint64_t id = atomic_fetch_add(&es->next_id, 1);
            es->subs[event][i] = (mlc__subscriber){
                .fn = fn, .userdata = userdata, .id = id, .active = true
            };
            pthread_rwlock_unlock(&es->lock);
            if (id_out) *id_out = id;
            return MLC_OK;
        }
    }
    pthread_rwlock_unlock(&es->lock);
    return MLC_ERR_FULL;
}

static void mlc__events_unsubscribe(mlc__event_system *es, uint64_t id) {
    pthread_rwlock_wrlock(&es->lock);
    for (int e = 0; e < MLC_EVENT_COUNT; e++) {
        for (int i = 0; i < MLC_MAX_SUBSCRIBERS; i++) {
            if (es->subs[e][i].active && es->subs[e][i].id == id) {
                es->subs[e][i].active = false;
                pthread_rwlock_unlock(&es->lock);
                return;
            }
        }
    }
    pthread_rwlock_unlock(&es->lock);
}

static void mlc__events_fire(mlc__event_system *es, mlc_event event, const void *detail) {
    if ((int)event < 0 || (int)event >= MLC_EVENT_COUNT) return;
    pthread_rwlock_rdlock(&es->lock);
    for (int i = 0; i < MLC_MAX_SUBSCRIBERS; i++) {
        mlc__subscriber *sub = &es->subs[event][i];
        if (sub->active) {
            int ret = sub->fn(event, detail, sub->userdata);
            if (ret != 0) sub->active = false; // unsubscribe
        }
    }
    pthread_rwlock_unlock(&es->lock);
}

// ═══════════════════════════════════════════════════════════════════════════
// Latency Tracking
// ═══════════════════════════════════════════════════════════════════════════

static void mlc__latency_record(mlc__latency_tracker *lt, uint64_t us) {
    atomic_fetch_add(&lt->count, 1);
    atomic_fetch_add(&lt->sum_us, us);
    // Map to log2 bucket: bucket 0 = [0,1us), bucket 1 = [1,2us), bucket 2 = [2,4us), etc.
    int bucket = 0;
    uint64_t v = us;
    while (v > 0 && bucket < MLC_LATENCY_BUCKETS - 1) {
        v >>= 1;
        bucket++;
    }
    atomic_fetch_add(&lt->buckets[bucket], 1);
}

static double mlc__latency_avg(const mlc__latency_tracker *lt) {
    uint64_t cnt = atomic_load(&lt->count);
    if (cnt == 0) return 0.0;
    return (double)atomic_load(&lt->sum_us) / (double)cnt;
}

static double mlc__latency_p99(const mlc__latency_tracker *lt) {
    uint64_t cnt = atomic_load(&lt->count);
    if (cnt == 0) return 0.0;
    uint64_t target = cnt * 99 / 100;
    uint64_t cumulative = 0;
    for (int i = 0; i < MLC_LATENCY_BUCKETS; i++) {
        cumulative += atomic_load(&lt->buckets[i]);
        if (cumulative >= target) {
            // Upper bound of this bucket: 2^i microseconds.
            return (double)(1ULL << i);
        }
    }
    return (double)(1ULL << (MLC_LATENCY_BUCKETS - 1));
}

// ═══════════════════════════════════════════════════════════════════════════
// CLOCK Eviction — HOT Tier
// ═══════════════════════════════════════════════════════════════════════════

// Evict unpinned entries from HOT until below low-water mark.
// Caller must hold write lock on hot_lock.
static void mlc__hot_evict(mlc_cache *c) {
    size_t budget = c->hot_budget;
    size_t low_target = (size_t)((double)budget * MLC_HOT_LOW_WATER);
    size_t used = atomic_load(&c->hot_bytes);
    if (used <= low_target) return;

    uint64_t threshold = atomic_load(&c->clock);
    uint64_t items_evicted = 0;
    uint64_t bytes_freed = 0;
    uint32_t full_sweeps = 0;

    // CLOCK sweep: scan from hand, evict entries that haven't been accessed recently.
    while (used > low_target && full_sweeps < 3) {
        bool evicted_any = false;
        for (uint32_t i = 0; i < c->hot.cap && used > low_target; i++) {
            uint32_t idx = (c->hot_clock_hand + i) & c->hot.mask;
            mlc__hot_slot *s = &c->hot.slots[idx];
            if (!s->occupied) continue;

            // Don't evict pinned entries.
            if (atomic_load(&s->val.pin_count) > 0) continue;

            uint64_t access = atomic_load(&s->val.last_access);
            if (access >= threshold - (threshold / 4)) {
                // Recently accessed: give second chance by clearing access time slightly.
                // We don't fully reset, just age it.
                atomic_store(&s->val.last_access, access - 1);
                continue;
            }

            // Spatial grouping check: count hot siblings in same chunk.
            mlc_chunk_key ck = mlc_mini_to_chunk(s->key);
            int hot_siblings = 0;
            int total_checked = 0;
            // Quick sample: check 4 random siblings (not all 512).
            for (int dx = 0; dx < 2 && total_checked < 4; dx++)
              for (int dy = 0; dy < 2 && total_checked < 4; dy++) {
                mlc_mini_key sib = {
                    .lod = s->key.lod,
                    .mx = ck.cx * MLC_MINIS_PER_AXIS + dx,
                    .my = ck.cy * MLC_MINIS_PER_AXIS + dy,
                    .mz = s->key.mz, // same Z
                };
                if (mlc_mini_key_eq(sib, s->key)) { total_checked++; continue; }
                if (mlc__hot_find(&c->hot, sib)) hot_siblings++;
                total_checked++;
              }
            // If most siblings are hot, skip this one (spatial locality).
            if (hot_siblings >= 3) continue;

            // Evict.
            if (s->val.data) MLC_FREE(s->val.data);
            s->occupied = false;
            c->hot.count--;
            // Backward-shift for robin-hood correctness.
            uint32_t prev = idx;
            uint32_t cur = (idx + 1) & c->hot.mask;
            while (c->hot.slots[cur].occupied && c->hot.slots[cur].psl > 0) {
                c->hot.slots[prev] = c->hot.slots[cur];
                c->hot.slots[prev].psl--;
                c->hot.slots[cur].occupied = false;
                prev = cur;
                cur = (cur + 1) & c->hot.mask;
            }

            used -= MLC_MINI_VOX;
            items_evicted++;
            bytes_freed += MLC_MINI_VOX;
            evicted_any = true;
        }
        c->hot_clock_hand = (c->hot_clock_hand + c->hot.cap) & c->hot.mask;
        if (!evicted_any) full_sweeps++;
        else full_sweeps = 0;
    }

    atomic_store(&c->hot_bytes, used);
    atomic_fetch_add(&c->counters.evictions[MLC_TIER_HOT], items_evicted);

    if (items_evicted > 0) {
        mlc_event_eviction detail = {
            .tier = MLC_TIER_HOT,
            .items_evicted = items_evicted,
            .bytes_freed = bytes_freed,
        };
        mlc__events_fire(&c->events, MLC_EVENT_EVICTION, &detail);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// CLOCK Eviction — WARM Tier
// ═══════════════════════════════════════════════════════════════════════════

static void mlc__warm_evict(mlc_cache *c) {
    size_t budget = c->warm_budget;
    size_t low_target = (size_t)((double)budget * MLC_WARM_LOW_WATER);
    size_t used = atomic_load(&c->warm_bytes);
    if (used <= low_target) return;

    uint64_t threshold = atomic_load(&c->clock);
    uint64_t items_evicted = 0;
    uint64_t bytes_freed = 0;
    uint32_t full_sweeps = 0;

    while (used > low_target && full_sweeps < 3) {
        bool evicted_any = false;
        for (uint32_t i = 0; i < c->warm.cap && used > low_target; i++) {
            uint32_t idx = (c->warm_clock_hand + i) & c->warm.mask;
            mlc__warm_slot *s = &c->warm.slots[idx];
            if (!s->occupied) continue;
            if (atomic_load(&s->val.pin_count) > 0) continue;

            uint64_t access = atomic_load(&s->val.last_access);
            if (access >= threshold - (threshold / 4)) {
                atomic_store(&s->val.last_access, access - 1);
                continue;
            }

            uint32_t entry_sz = s->val.h264_len;
            if (s->val.h264_data) MLC_FREE(s->val.h264_data);
            s->occupied = false;
            c->warm.count--;

            // Backward-shift.
            uint32_t prev = idx;
            uint32_t cur = (idx + 1) & c->warm.mask;
            while (c->warm.slots[cur].occupied && c->warm.slots[cur].psl > 0) {
                c->warm.slots[prev] = c->warm.slots[cur];
                c->warm.slots[prev].psl--;
                c->warm.slots[cur].occupied = false;
                prev = cur;
                cur = (cur + 1) & c->warm.mask;
            }

            used -= entry_sz;
            items_evicted++;
            bytes_freed += entry_sz;
            evicted_any = true;
        }
        c->warm_clock_hand = (c->warm_clock_hand + c->warm.cap) & c->warm.mask;
        if (!evicted_any) full_sweeps++;
        else full_sweeps = 0;
    }

    atomic_store(&c->warm_bytes, used);
    atomic_fetch_add(&c->counters.evictions[MLC_TIER_WARM], items_evicted);

    if (items_evicted > 0) {
        mlc_event_eviction detail = {
            .tier = MLC_TIER_WARM,
            .items_evicted = items_evicted,
            .bytes_freed = bytes_freed,
        };
        mlc__events_fire(&c->events, MLC_EVENT_EVICTION, &detail);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// HOT Tier Promotion (insert mini-cube)
// ═══════════════════════════════════════════════════════════════════════════

// Promote a decoded mini-cube into HOT tier. Evicts if needed.
// data must be a 4KB page-aligned buffer that the cache takes ownership of.
static mlc_status mlc__promote_hot(mlc_cache *c, mlc_mini_key key, uint8_t *data) {
    pthread_rwlock_wrlock(&c->hot_lock);

    // Already present? Update access time and skip.
    mlc__hot_entry *existing = mlc__hot_find(&c->hot, key);
    if (existing) {
        atomic_store(&existing->last_access, atomic_fetch_add(&c->clock, 1));
        pthread_rwlock_unlock(&c->hot_lock);
        MLC_FREE(data); // caller's copy not needed
        return MLC_OK;
    }

    // Check budget and evict if needed.
    size_t used = atomic_load(&c->hot_bytes);
    if (used + MLC_MINI_VOX > (size_t)((double)c->hot_budget * MLC_HOT_HIGH_WATER)) {
        mlc__hot_evict(c);
    }

    mlc__hot_entry entry = {
        .data = data,
        .last_access = atomic_fetch_add(&c->clock, 1),
        .pin_count = 0,
        .flags = 0,
    };

    mlc_status s = mlc__hot_insert(&c->hot, key, entry);
    if (s == MLC_OK) {
        atomic_fetch_add(&c->hot_bytes, MLC_MINI_VOX);
    } else {
        MLC_FREE(data);
    }

    pthread_rwlock_unlock(&c->hot_lock);
    return s;
}

// ═══════════════════════════════════════════════════════════════════════════
// WARM Tier Promotion (insert compressed chunk)
// ═══════════════════════════════════════════════════════════════════════════

// Promote a compressed H264 chunk into WARM tier. Takes ownership of h264_data.
static mlc_status mlc__promote_warm(mlc_cache *c, mlc_chunk_key key,
                                     uint8_t *h264_data, uint32_t h264_len) {
    pthread_rwlock_wrlock(&c->warm_lock);

    mlc__warm_entry *existing = mlc__warm_find(&c->warm, key);
    if (existing) {
        atomic_store(&existing->last_access, atomic_fetch_add(&c->clock, 1));
        pthread_rwlock_unlock(&c->warm_lock);
        MLC_FREE(h264_data);
        return MLC_OK;
    }

    size_t used = atomic_load(&c->warm_bytes);
    if (used + h264_len > (size_t)((double)c->warm_budget * MLC_WARM_HIGH_WATER)) {
        mlc__warm_evict(c);
    }

    mlc__warm_entry entry = {
        .h264_data = h264_data,
        .h264_len = h264_len,
        .last_access = atomic_fetch_add(&c->clock, 1),
        .pin_count = 0,
    };

    mlc_status s = mlc__warm_insert(&c->warm, key, entry);
    if (s == MLC_OK) {
        atomic_fetch_add(&c->warm_bytes, h264_len);
    } else {
        MLC_FREE(h264_data);
    }

    pthread_rwlock_unlock(&c->warm_lock);
    return s;
}

// ═══════════════════════════════════════════════════════════════════════════
// Mini-Cube Extraction from Decoded Chunk
// ═══════════════════════════════════════════════════════════════════════════

// Extract a 16^3 mini-cube from a decoded 128^3 chunk into a new page-aligned buffer.
static uint8_t *mlc__extract_mini(const uint8_t *chunk_data, int lx, int ly, int lz) {
    uint8_t *mini = (uint8_t *)mlc__alloc_aligned(MLC_PAGE_SIZE, MLC_MINI_VOX);
    if (!mini) return NULL;

    int ox = lx * MLC_MINI_DIM;
    int oy = ly * MLC_MINI_DIM;
    int oz = lz * MLC_MINI_DIM;

    for (int z = 0; z < MLC_MINI_DIM; z++) {
        for (int y = 0; y < MLC_MINI_DIM; y++) {
            memcpy(mini + (z * MLC_MINI_DIM + y) * MLC_MINI_DIM,
                   chunk_data + ((oz + z) * MLC_CHUNK_DIM + (oy + y)) * MLC_CHUNK_DIM + ox,
                   MLC_MINI_DIM);
        }
    }
    return mini;
}

// Decode a chunk and promote all 512 minis to HOT tier.
static mlc_status mlc__decode_chunk_to_hot(mlc_cache *c, mlc_chunk_key ck,
                                            const uint8_t *decoded_chunk) {
    for (int lz = 0; lz < MLC_MINIS_PER_AXIS; lz++) {
        for (int ly = 0; ly < MLC_MINIS_PER_AXIS; ly++) {
            for (int lx = 0; lx < MLC_MINIS_PER_AXIS; lx++) {
                uint8_t *mini = mlc__extract_mini(decoded_chunk, lx, ly, lz);
                if (!mini) return MLC_ERR_ALLOC;

                mlc_mini_key mk = {
                    .lod = ck.lod,
                    .mx = ck.cx * MLC_MINIS_PER_AXIS + lx,
                    .my = ck.cy * MLC_MINIS_PER_AXIS + ly,
                    .mz = ck.cz * MLC_MINIS_PER_AXIS + lz,
                };
                mlc_status s = mlc__promote_hot(c, mk, mini);
                if (s != MLC_OK) return s;
            }
        }
    }

    // Fire chunk decoded event.
    mlc_event_chunk_decoded detail = { .key = ck };
    mlc__events_fire(&c->events, MLC_EVENT_CHUNK_DECODED, &detail);

    return MLC_OK;
}

// ═══════════════════════════════════════════════════════════════════════════
// Fetch Engine — Worker Threads
// ═══════════════════════════════════════════════════════════════════════════

// Decode worker: pulls from priority queue, decodes WARM->HOT.
// Helper: fire all waiters for a completed fetch and remove from inflight.
static void mlc__complete_fetch(mlc_cache *c, mlc_chunk_key ck,
                                 const uint8_t *decoded_chunk) {
    mlc__fetch_engine *fe = &c->fetch;

    pthread_mutex_lock(&fe->mu);
    mlc__waiter *waiters = mlc__inflight_remove(&fe->inflight, ck);
    pthread_mutex_unlock(&fe->mu);

    while (waiters) {
        mlc__waiter *next = waiters->next;
        if (waiters->chunk_cb) {
            waiters->chunk_cb(ck, decoded_chunk, waiters->userdata);
        }
        if (waiters->mini_cb) {
            // For mini callbacks: find the mini they wanted and call with its data.
            // Since we don't track which specific mini was requested, call with NULL
            // to signal "your data is now in HOT, re-query".
            mlc_mini_key mk = {
                .lod = ck.lod,
                .mx = ck.cx * MLC_MINIS_PER_AXIS,
                .my = ck.cy * MLC_MINIS_PER_AXIS,
                .mz = ck.cz * MLC_MINIS_PER_AXIS,
            };
            waiters->mini_cb(mk, NULL, waiters->userdata);
        }
        MLC_FREE(waiters);
        waiters = next;
    }
}

// Try to decode compressed chunk from WARM → decoded buffer → promote all minis to HOT.
// Returns true if successful.
static bool mlc__try_decode_warm(mlc_cache *c, mlc_chunk_key ck) {
    if (!c->decode_fn) return false;

    // Get compressed data from WARM.
    pthread_rwlock_rdlock(&c->warm_lock);
    mlc__warm_entry *we = mlc__warm_find(&c->warm, ck);
    uint8_t *h264_copy = NULL;
    uint32_t h264_len = 0;
    if (we) {
        h264_len = we->h264_len;
        h264_copy = (uint8_t *)MLC_MALLOC(h264_len);
        if (h264_copy) memcpy(h264_copy, we->h264_data, h264_len);
        atomic_store(&we->last_access, atomic_fetch_add(&c->clock, 1));
        atomic_fetch_add(&c->counters.hits[MLC_TIER_WARM], 1);
    } else {
        atomic_fetch_add(&c->counters.misses[MLC_TIER_WARM], 1);
    }
    pthread_rwlock_unlock(&c->warm_lock);

    if (!h264_copy) return false;

    // Decode.
    uint8_t *decoded = (uint8_t *)MLC_MALLOC(MLC_CHUNK_VOX);
    if (!decoded) { MLC_FREE(h264_copy); return false; }

    mlc_status ds = c->decode_fn(h264_copy, h264_len, decoded, c->decode_userdata);
    MLC_FREE(h264_copy);

    if (ds != MLC_OK) {
        MLC_FREE(decoded);
        return false;
    }

    // Extract all 512 minis and promote to HOT.
    mlc__decode_chunk_to_hot(c, ck, decoded);
    MLC_FREE(decoded);
    return true;
}

static void *mlc__decode_worker(void *arg) {
    mlc_cache *c = (mlc_cache *)arg;
    mlc__fetch_engine *fe = &c->fetch;

    while (!atomic_load(&fe->stop)) {
        mlc__fetch_req req;

        // Wait for work.
        pthread_mutex_lock(&fe->mu);
        while (fe->pq.count == 0 && !atomic_load(&fe->stop)) {
            pthread_cond_wait(&fe->cv, &fe->mu);
        }
        if (atomic_load(&fe->stop)) {
            pthread_mutex_unlock(&fe->mu);
            break;
        }
        if (!mlc__pq_pop(&fe->pq, &req)) {
            pthread_mutex_unlock(&fe->mu);
            continue;
        }
        pthread_mutex_unlock(&fe->mu);

        // Check if already in HOT (first mini present = chunk already decoded).
        mlc_mini_key first_mini = {
            .lod = req.key.lod,
            .mx = req.key.cx * MLC_MINIS_PER_AXIS,
            .my = req.key.cy * MLC_MINIS_PER_AXIS,
            .mz = req.key.cz * MLC_MINIS_PER_AXIS,
        };
        pthread_rwlock_rdlock(&c->hot_lock);
        bool in_hot = mlc__hot_find(&c->hot, first_mini) != NULL;
        pthread_rwlock_unlock(&c->hot_lock);

        if (in_hot) {
            mlc__complete_fetch(c, req.key, NULL);
            continue;
        }

        // Try WARM tier: decode compressed → HOT.
        if (mlc__try_decode_warm(c, req.key)) {
            mlc__complete_fetch(c, req.key, NULL);
            continue;
        }

        // TODO Phase 3: Try COLD tier (zarr_read_chunk → decode → HOT).
        // TODO Phase 4: Try ICE tier (S3 range request → WARM → decode → HOT).

        // Fetch failed — still remove from inflight and notify waiters.
        mlc__complete_fetch(c, req.key, NULL);
    }
    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// Fetch Engine Init/Destroy
// ═══════════════════════════════════════════════════════════════════════════

static mlc_status mlc__fetch_init(mlc_cache *c) {
    mlc__fetch_engine *fe = &c->fetch;
    mlc_status s;

    s = mlc__pq_init(&fe->pq, 1024);
    if (s != MLC_OK) return s;

    s = mlc__inflight_init(&fe->inflight, 512);
    if (s != MLC_OK) { mlc__pq_free(&fe->pq); return s; }

    pthread_mutex_init(&fe->mu, NULL);
    pthread_cond_init(&fe->cv, NULL);
    atomic_store(&fe->stop, false);
    fe->next_ts = 0;

    int n_decode = c->config.threads.decode_workers;
    int n_io = c->config.threads.io_workers;

    fe->n_decode = n_decode;
    fe->n_io = n_io;

    fe->decode_threads = (pthread_t *)MLC_CALLOC(n_decode, sizeof(pthread_t));
    fe->io_threads = (pthread_t *)MLC_CALLOC(n_io, sizeof(pthread_t));
    if (!fe->decode_threads || !fe->io_threads) return MLC_ERR_ALLOC;

    for (int i = 0; i < n_decode; i++) {
        if (pthread_create(&fe->decode_threads[i], NULL, mlc__decode_worker, c) != 0) {
            // Partial creation: stop what we have.
            atomic_store(&fe->stop, true);
            pthread_cond_broadcast(&fe->cv);
            for (int j = 0; j < i; j++) pthread_join(fe->decode_threads[j], NULL);
            return MLC_ERR_ALLOC;
        }
    }

    // IO threads are stubs for now (Phase 3).
    fe->n_io = 0; // Don't start IO threads yet.

    return MLC_OK;
}

static void mlc__fetch_destroy(mlc__fetch_engine *fe) {
    atomic_store(&fe->stop, true);
    pthread_cond_broadcast(&fe->cv);

    for (int i = 0; i < fe->n_decode; i++) {
        pthread_join(fe->decode_threads[i], NULL);
    }
    for (int i = 0; i < fe->n_io; i++) {
        pthread_join(fe->io_threads[i], NULL);
    }

    MLC_FREE(fe->decode_threads);
    MLC_FREE(fe->io_threads);
    mlc__pq_free(&fe->pq);
    mlc__inflight_free(&fe->inflight);
    pthread_mutex_destroy(&fe->mu);
    pthread_cond_destroy(&fe->cv);
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_config mlc_default_config(void) {
    int ncpu = mlc__ncpu();
    return (mlc_config){
        .volume = {
            .shape = {0, 0, 0},
            .lod_levels = 1,
            .downsample_factor = 2,
            .qp = MLC_QP_NEAR_LOSSLESS,
        },
        .tiers = {
            .hot_budget = 2ULL << 30,     // 2 GB
            .warm_budget = 512ULL << 20,  // 512 MB
            .cold_root = {0},
            .ice_bucket = {0},
            .ice_prefix = {0},
            .ice_s3_config = NULL,
        },
        .threads = {
            .decode_workers = ncpu > 2 ? ncpu / 2 : 1,
            .io_workers = 4,
            .net_workers = 4,
        },
        .adaptive_budgets = false,
        .stats_interval_ms = 0,
    };
}

MLCDEF mlc_status mlc_create(mlc_cache **out, const mlc_config *config) {
    if (!out || !config) return MLC_ERR_NULL_ARG;

    mlc_cache *c = (mlc_cache *)MLC_CALLOC(1, sizeof(mlc_cache));
    if (!c) return MLC_ERR_ALLOC;

    c->config = *config;
    c->hot_budget = config->tiers.hot_budget;
    c->warm_budget = config->tiers.warm_budget;
    atomic_store(&c->clock, 1);
    atomic_store(&c->hot_bytes, 0);
    atomic_store(&c->warm_bytes, 0);
    atomic_store(&c->shutting_down, false);
    c->hot_clock_hand = 0;
    c->warm_clock_hand = 0;

    // Estimate initial HOT table capacity.
    uint32_t hot_est = (uint32_t)(config->tiers.hot_budget / MLC_MINI_VOX);
    hot_est = hot_est > MLC_HT_INIT_CAP ? hot_est : MLC_HT_INIT_CAP;

    mlc_status s = mlc__hot_init(&c->hot, hot_est);
    if (s != MLC_OK) { MLC_FREE(c); return s; }

    // Estimate WARM table capacity (assume ~500KB avg compressed chunk).
    uint32_t warm_est = (uint32_t)(config->tiers.warm_budget / (500 * 1024));
    warm_est = warm_est > MLC_HT_INIT_CAP ? warm_est : MLC_HT_INIT_CAP;

    s = mlc__warm_init(&c->warm, warm_est);
    if (s != MLC_OK) { mlc__hot_free(&c->hot); MLC_FREE(c); return s; }

    pthread_rwlock_init(&c->hot_lock, NULL);
    pthread_rwlock_init(&c->warm_lock, NULL);
    pthread_mutex_init(&c->prefetch_lock, NULL);

    // Event system.
    mlc__events_init(&c->events);

    // Zero counters and latency.
    memset(&c->counters, 0, sizeof(c->counters));
    memset(&c->latency, 0, sizeof(c->latency));

    // Budget controller.
    c->budget = (mlc__budget_ctrl){
        .hot_hit_ema = 0.0,
        .warm_hit_ema = 0.0,
        .alpha = 0.1,
        .total_ram = config->tiers.hot_budget + config->tiers.warm_budget,
        .enabled = config->adaptive_budgets,
    };

    // Prefetch state.
    memset(&c->prefetch, 0, sizeof(c->prefetch));

    // Codec callbacks (NULL until set by mlc_set_decoder/encoder).
    c->decode_fn = NULL;
    c->decode_userdata = NULL;
    c->encode_fn = NULL;
    c->encode_userdata = NULL;

    // Scroll hint state.
    memset(&c->scroll_hint, 0, sizeof(c->scroll_hint));

    // Fetch engine (starts worker threads).
    s = mlc__fetch_init(c);
    if (s != MLC_OK) {
        mlc__hot_free(&c->hot);
        mlc__warm_free(&c->warm);
        mlc__events_destroy(&c->events);
        pthread_rwlock_destroy(&c->hot_lock);
        pthread_rwlock_destroy(&c->warm_lock);
        pthread_mutex_destroy(&c->prefetch_lock);
        MLC_FREE(c);
        return s;
    }

    *out = c;
    return MLC_OK;
}

MLCDEF void mlc_destroy(mlc_cache *c) {
    if (!c) return;
    atomic_store(&c->shutting_down, true);

    mlc__fetch_destroy(&c->fetch);

    mlc__hot_free(&c->hot);
    mlc__warm_free(&c->warm);
    mlc__events_destroy(&c->events);

    pthread_rwlock_destroy(&c->hot_lock);
    pthread_rwlock_destroy(&c->warm_lock);
    pthread_mutex_destroy(&c->prefetch_lock);

    MLC_FREE(c);
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Synchronous Data Access
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_status mlc_get_mini(mlc_cache *c, mlc_mini_key key,
                                const uint8_t **data_out) {
    if (!c || !data_out) return MLC_ERR_NULL_ARG;
    if (atomic_load(&c->shutting_down)) return MLC_ERR_SHUTDOWN;

    uint64_t t0 = mlc__now_us();
    atomic_fetch_add(&c->counters.total_requests, 1);

    // Fast path: HOT tier lookup.
    pthread_rwlock_rdlock(&c->hot_lock);
    mlc__hot_entry *he = mlc__hot_find(&c->hot, key);
    if (he) {
        atomic_store(&he->last_access, atomic_fetch_add(&c->clock, 1));
        atomic_fetch_add(&he->pin_count, 1);
        *data_out = he->data;
        pthread_rwlock_unlock(&c->hot_lock);

        atomic_fetch_add(&c->counters.hits[MLC_TIER_HOT], 1);
        mlc__latency_record(&c->latency[MLC_TIER_HOT], mlc__now_us() - t0);
        return MLC_OK;
    }
    pthread_rwlock_unlock(&c->hot_lock);
    atomic_fetch_add(&c->counters.misses[MLC_TIER_HOT], 1);

    // HOT miss. Need to fetch the containing chunk and decode.
    mlc_chunk_key ck = mlc_mini_to_chunk(key);

    // Check WARM tier for compressed chunk.
    pthread_rwlock_rdlock(&c->warm_lock);
    mlc__warm_entry *we = mlc__warm_find(&c->warm, ck);
    uint8_t *h264_copy = NULL;
    uint32_t h264_len = 0;
    if (we) {
        h264_len = we->h264_len;
        h264_copy = (uint8_t *)MLC_MALLOC(h264_len);
        if (h264_copy) memcpy(h264_copy, we->h264_data, h264_len);
        atomic_store(&we->last_access, atomic_fetch_add(&c->clock, 1));
        atomic_fetch_add(&c->counters.hits[MLC_TIER_WARM], 1);
    } else {
        atomic_fetch_add(&c->counters.misses[MLC_TIER_WARM], 1);
    }
    pthread_rwlock_unlock(&c->warm_lock);

    if (h264_copy) {
        if (!c->decode_fn) {
            MLC_FREE(h264_copy);
            mlc__latency_record(&c->latency[MLC_TIER_WARM], mlc__now_us() - t0);
            return MLC_ERR_CODEC; // no decoder registered
        }

        // Decode compressed chunk to temporary buffer.
        uint8_t *decoded = (uint8_t *)MLC_MALLOC(MLC_CHUNK_VOX);
        if (!decoded) { MLC_FREE(h264_copy); return MLC_ERR_ALLOC; }

        mlc_status ds = c->decode_fn(h264_copy, h264_len, decoded, c->decode_userdata);
        MLC_FREE(h264_copy);

        if (ds != MLC_OK) {
            MLC_FREE(decoded);
            mlc__latency_record(&c->latency[MLC_TIER_WARM], mlc__now_us() - t0);
            return MLC_ERR_CODEC;
        }

        // Extract all 512 minis to HOT.
        mlc__decode_chunk_to_hot(c, ck, decoded);
        MLC_FREE(decoded);

        // Now re-query HOT — our mini should be there.
        pthread_rwlock_rdlock(&c->hot_lock);
        he = mlc__hot_find(&c->hot, key);
        if (he) {
            atomic_store(&he->last_access, atomic_fetch_add(&c->clock, 1));
            atomic_fetch_add(&he->pin_count, 1);
            *data_out = he->data;
            pthread_rwlock_unlock(&c->hot_lock);
            mlc__latency_record(&c->latency[MLC_TIER_WARM], mlc__now_us() - t0);
            return MLC_OK;
        }
        pthread_rwlock_unlock(&c->hot_lock);
        mlc__latency_record(&c->latency[MLC_TIER_WARM], mlc__now_us() - t0);
        return MLC_ERR_ALLOC; // extraction failed (budget too small?)
    }

    // TODO Phase 3: Try COLD tier.
    // TODO Phase 4: Try ICE tier.

    mlc__latency_record(&c->latency[MLC_TIER_WARM], mlc__now_us() - t0);
    return MLC_ERR_NOT_FOUND;
}

MLCDEF void mlc_unpin_mini(mlc_cache *c, mlc_mini_key key) {
    if (!c) return;
    pthread_rwlock_rdlock(&c->hot_lock);
    mlc__hot_entry *he = mlc__hot_find(&c->hot, key);
    if (he) {
        int32_t prev = atomic_fetch_sub(&he->pin_count, 1);
        (void)prev; // pin_count should not go below 0 in correct usage
    }
    pthread_rwlock_unlock(&c->hot_lock);
}

MLCDEF mlc_status mlc_get_chunk(mlc_cache *c, mlc_chunk_key key,
                                 const uint8_t **data_out) {
    if (!c || !data_out) return MLC_ERR_NULL_ARG;
    if (atomic_load(&c->shutting_down)) return MLC_ERR_SHUTDOWN;

    // For chunk-level access, we allocate a temporary 2MB buffer and
    // assemble from HOT minis if all are present, or fetch+decode.
    // For Phase 1, check if all minis are in HOT.

    uint8_t *buf = (uint8_t *)MLC_MALLOC(MLC_CHUNK_VOX);
    if (!buf) return MLC_ERR_ALLOC;

    bool all_present = true;
    pthread_rwlock_rdlock(&c->hot_lock);
    for (int lz = 0; lz < MLC_MINIS_PER_AXIS && all_present; lz++) {
        for (int ly = 0; ly < MLC_MINIS_PER_AXIS && all_present; ly++) {
            for (int lx = 0; lx < MLC_MINIS_PER_AXIS && all_present; lx++) {
                mlc_mini_key mk = {
                    .lod = key.lod,
                    .mx = key.cx * MLC_MINIS_PER_AXIS + lx,
                    .my = key.cy * MLC_MINIS_PER_AXIS + ly,
                    .mz = key.cz * MLC_MINIS_PER_AXIS + lz,
                };
                mlc__hot_entry *he = mlc__hot_find(&c->hot, mk);
                if (!he) { all_present = false; break; }
                // Copy mini data into chunk buffer at correct position.
                int ox = lx * MLC_MINI_DIM;
                int oy = ly * MLC_MINI_DIM;
                int oz = lz * MLC_MINI_DIM;
                for (int z = 0; z < MLC_MINI_DIM; z++) {
                    for (int y = 0; y < MLC_MINI_DIM; y++) {
                        memcpy(buf + ((oz + z) * MLC_CHUNK_DIM + (oy + y)) * MLC_CHUNK_DIM + ox,
                               he->data + (z * MLC_MINI_DIM + y) * MLC_MINI_DIM,
                               MLC_MINI_DIM);
                    }
                }
            }
        }
    }
    pthread_rwlock_unlock(&c->hot_lock);

    if (!all_present) {
        MLC_FREE(buf);
        return MLC_ERR_NOT_FOUND; // TODO: fetch through tiers
    }

    *data_out = buf;
    return MLC_OK;
}

MLCDEF void mlc_unpin_chunk(mlc_cache *c, mlc_chunk_key key) {
    // Chunk data is a temporary copy, not pinned.
    // The caller is expected to free via MLC_FREE.
    // For now this is a no-op since mlc_get_chunk returns a copy.
    (void)c; (void)key;
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Async Data Access
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_status mlc_request_mini(mlc_cache *c, mlc_mini_key key, mlc_priority priority,
                                    mlc_mini_ready_fn callback, void *userdata) {
    if (!c) return MLC_ERR_NULL_ARG;
    if (atomic_load(&c->shutting_down)) return MLC_ERR_SHUTDOWN;

    // Check if already in HOT.
    pthread_rwlock_rdlock(&c->hot_lock);
    mlc__hot_entry *he = mlc__hot_find(&c->hot, key);
    if (he) {
        atomic_store(&he->last_access, atomic_fetch_add(&c->clock, 1));
        const uint8_t *data = he->data;
        pthread_rwlock_unlock(&c->hot_lock);
        if (callback) callback(key, data, userdata);
        return MLC_OK;
    }
    pthread_rwlock_unlock(&c->hot_lock);

    // Need to fetch the containing chunk.
    mlc_chunk_key ck = mlc_mini_to_chunk(key);

    pthread_mutex_lock(&c->fetch.mu);

    // Dedup check.
    mlc__inflight_entry *ie = mlc__inflight_find(&c->fetch.inflight, ck);
    if (ie) {
        // Already inflight — add waiter.
        mlc__waiter *w = (mlc__waiter *)MLC_MALLOC(sizeof(mlc__waiter));
        if (!w) { pthread_mutex_unlock(&c->fetch.mu); return MLC_ERR_ALLOC; }
        *w = (mlc__waiter){ .mini_cb = callback, .chunk_cb = NULL,
                            .userdata = userdata, .next = ie->waiters };
        ie->waiters = w;
        atomic_fetch_add(&c->counters.dedup_count, 1);
        // Try to escalate priority.
        mlc__pq_escalate(&c->fetch.pq, ck, priority);
        pthread_mutex_unlock(&c->fetch.mu);
        return MLC_OK;
    }

    // New fetch request.
    mlc_status s = mlc__inflight_insert(&c->fetch.inflight, ck);
    if (s != MLC_OK) { pthread_mutex_unlock(&c->fetch.mu); return s; }

    // Add callback as first waiter.
    if (callback) {
        mlc__waiter *w = (mlc__waiter *)MLC_MALLOC(sizeof(mlc__waiter));
        if (w) {
            *w = (mlc__waiter){ .mini_cb = callback, .chunk_cb = NULL,
                                .userdata = userdata, .next = NULL };
            mlc__inflight_entry *entry = mlc__inflight_find(&c->fetch.inflight, ck);
            if (entry) entry->waiters = w;
        }
    }

    mlc__fetch_req req = {
        .key = ck,
        .priority = priority,
        .distance_sq = 0.0f, // TODO: compute from viewpoint
        .timestamp = c->fetch.next_ts++,
    };
    mlc__pq_push(&c->fetch.pq, req);
    pthread_cond_signal(&c->fetch.cv);
    pthread_mutex_unlock(&c->fetch.mu);

    return MLC_OK;
}

MLCDEF mlc_status mlc_request_chunk(mlc_cache *c, mlc_chunk_key key, mlc_priority priority,
                                     mlc_chunk_ready_fn callback, void *userdata) {
    if (!c) return MLC_ERR_NULL_ARG;
    if (atomic_load(&c->shutting_down)) return MLC_ERR_SHUTDOWN;

    pthread_mutex_lock(&c->fetch.mu);

    mlc__inflight_entry *ie = mlc__inflight_find(&c->fetch.inflight, key);
    if (ie) {
        mlc__waiter *w = (mlc__waiter *)MLC_MALLOC(sizeof(mlc__waiter));
        if (!w) { pthread_mutex_unlock(&c->fetch.mu); return MLC_ERR_ALLOC; }
        *w = (mlc__waiter){ .mini_cb = NULL, .chunk_cb = callback,
                            .userdata = userdata, .next = ie->waiters };
        ie->waiters = w;
        atomic_fetch_add(&c->counters.dedup_count, 1);
        mlc__pq_escalate(&c->fetch.pq, key, priority);
        pthread_mutex_unlock(&c->fetch.mu);
        return MLC_OK;
    }

    mlc_status s = mlc__inflight_insert(&c->fetch.inflight, key);
    if (s != MLC_OK) { pthread_mutex_unlock(&c->fetch.mu); return s; }

    if (callback) {
        mlc__waiter *w = (mlc__waiter *)MLC_MALLOC(sizeof(mlc__waiter));
        if (w) {
            *w = (mlc__waiter){ .mini_cb = NULL, .chunk_cb = callback,
                                .userdata = userdata, .next = NULL };
            mlc__inflight_entry *entry = mlc__inflight_find(&c->fetch.inflight, key);
            if (entry) entry->waiters = w;
        }
    }

    mlc__fetch_req req = {
        .key = key,
        .priority = priority,
        .distance_sq = 0.0f,
        .timestamp = c->fetch.next_ts++,
    };
    mlc__pq_push(&c->fetch.pq, req);
    pthread_cond_signal(&c->fetch.cv);
    pthread_mutex_unlock(&c->fetch.mu);

    return MLC_OK;
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Region Reads
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_status mlc_read_region(mlc_cache *c, mlc_region region,
                                   uint8_t *buf, size_t bufsize) {
    if (!c || !buf) return MLC_ERR_NULL_ARG;

    int32_t sx = region.x1 - region.x0;
    int32_t sy = region.y1 - region.y0;
    int32_t sz = region.z1 - region.z0;
    if (sx <= 0 || sy <= 0 || sz <= 0) return MLC_ERR_BOUNDS;
    if (bufsize < (size_t)sx * (size_t)sy * (size_t)sz) return MLC_ERR_BOUNDS;

    // Iterate over all minis that intersect the region.
    int32_t mx0 = region.x0 / MLC_MINI_DIM;
    int32_t my0 = region.y0 / MLC_MINI_DIM;
    int32_t mz0 = region.z0 / MLC_MINI_DIM;
    int32_t mx1 = (region.x1 - 1) / MLC_MINI_DIM;
    int32_t my1 = (region.y1 - 1) / MLC_MINI_DIM;
    int32_t mz1 = (region.z1 - 1) / MLC_MINI_DIM;

    for (int32_t mz = mz0; mz <= mz1; mz++) {
        for (int32_t my = my0; my <= my1; my++) {
            for (int32_t mx = mx0; mx <= mx1; mx++) {
                mlc_mini_key mk = { .lod = region.lod, .mx = mx, .my = my, .mz = mz };
                const uint8_t *mini_data;
                mlc_status s = mlc_get_mini(c, mk, &mini_data);
                if (s != MLC_OK) return s;

                // Copy the relevant portion of this mini into the output buffer.
                int32_t vx0 = mx * MLC_MINI_DIM;
                int32_t vy0 = my * MLC_MINI_DIM;
                int32_t vz0 = mz * MLC_MINI_DIM;

                int32_t src_x0 = region.x0 > vx0 ? region.x0 - vx0 : 0;
                int32_t src_y0 = region.y0 > vy0 ? region.y0 - vy0 : 0;
                int32_t src_z0 = region.z0 > vz0 ? region.z0 - vz0 : 0;
                int32_t src_x1 = region.x1 < vx0 + MLC_MINI_DIM ? region.x1 - vx0 : MLC_MINI_DIM;
                int32_t src_y1 = region.y1 < vy0 + MLC_MINI_DIM ? region.y1 - vy0 : MLC_MINI_DIM;
                int32_t src_z1 = region.z1 < vz0 + MLC_MINI_DIM ? region.z1 - vz0 : MLC_MINI_DIM;

                int32_t copy_w = src_x1 - src_x0;

                for (int32_t z = src_z0; z < src_z1; z++) {
                    for (int32_t y = src_y0; y < src_y1; y++) {
                        int32_t dst_x = vx0 + src_x0 - region.x0;
                        int32_t dst_y = vy0 + y - region.y0;
                        int32_t dst_z = vz0 + z - region.z0;
                        size_t dst_off = (size_t)dst_z * (size_t)sy * (size_t)sx
                                       + (size_t)dst_y * (size_t)sx
                                       + (size_t)dst_x;
                        size_t src_off = (size_t)z * MLC_MINI_DIM * MLC_MINI_DIM
                                       + (size_t)y * MLC_MINI_DIM
                                       + (size_t)src_x0;
                        memcpy(buf + dst_off, mini_data + src_off, copy_w);
                    }
                }

                mlc_unpin_mini(c, mk);
            }
        }
    }

    return MLC_OK;
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Prefetch
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_status mlc_prefetch_region(mlc_cache *c, mlc_region region, mlc_priority priority) {
    if (!c) return MLC_ERR_NULL_ARG;
    if (atomic_load(&c->shutting_down)) return MLC_ERR_SHUTDOWN;

    // Convert to chunk coordinates.
    int32_t cx0 = region.x0 / MLC_CHUNK_DIM;
    int32_t cy0 = region.y0 / MLC_CHUNK_DIM;
    int32_t cz0 = region.z0 / MLC_CHUNK_DIM;
    int32_t cx1 = (region.x1 - 1) / MLC_CHUNK_DIM;
    int32_t cy1 = (region.y1 - 1) / MLC_CHUNK_DIM;
    int32_t cz1 = (region.z1 - 1) / MLC_CHUNK_DIM;

    pthread_mutex_lock(&c->fetch.mu);
    for (int32_t cz = cz0; cz <= cz1; cz++) {
        for (int32_t cy = cy0; cy <= cy1; cy++) {
            for (int32_t cx = cx0; cx <= cx1; cx++) {
                mlc_chunk_key ck = { .lod = region.lod, .cx = cx, .cy = cy, .cz = cz };

                // Skip if already inflight.
                if (mlc__inflight_find(&c->fetch.inflight, ck)) {
                    mlc__pq_escalate(&c->fetch.pq, ck, priority);
                    continue;
                }

                // Skip if already in HOT (check first mini).
                mlc_mini_key first_mini = {
                    .lod = ck.lod,
                    .mx = ck.cx * MLC_MINIS_PER_AXIS,
                    .my = ck.cy * MLC_MINIS_PER_AXIS,
                    .mz = ck.cz * MLC_MINIS_PER_AXIS,
                };
                pthread_rwlock_rdlock(&c->hot_lock);
                bool hot = mlc__hot_find(&c->hot, first_mini) != NULL;
                pthread_rwlock_unlock(&c->hot_lock);
                if (hot) continue;

                mlc__inflight_insert(&c->fetch.inflight, ck);
                mlc__fetch_req req = {
                    .key = ck,
                    .priority = priority,
                    .distance_sq = 0.0f,
                    .timestamp = c->fetch.next_ts++,
                };
                mlc__pq_push(&c->fetch.pq, req);
                atomic_fetch_add(&c->counters.prefetch_issued, 1);
            }
        }
    }
    pthread_cond_broadcast(&c->fetch.cv);
    pthread_mutex_unlock(&c->fetch.mu);

    return MLC_OK;
}

MLCDEF mlc_status mlc_prefetch_around(mlc_cache *c, int32_t lod,
                                       int32_t vx, int32_t vy, int32_t vz,
                                       int32_t radius_chunks) {
    if (!c) return MLC_ERR_NULL_ARG;

    int32_t center_cx = vx / MLC_CHUNK_DIM;
    int32_t center_cy = vy / MLC_CHUNK_DIM;
    int32_t center_cz = vz / MLC_CHUNK_DIM;

    for (int32_t shell = 0; shell <= radius_chunks; shell++) {
        mlc_priority prio;
        if (shell == 0)      prio = MLC_PRIORITY_CRITICAL;
        else if (shell == 1) prio = MLC_PRIORITY_HIGH;
        else if (shell == 2) prio = MLC_PRIORITY_NORMAL;
        else                 prio = MLC_PRIORITY_LOW;

        // Iterate all chunks at Chebyshev distance == shell.
        for (int32_t dz = -shell; dz <= shell; dz++) {
            for (int32_t dy = -shell; dy <= shell; dy++) {
                for (int32_t dx = -shell; dx <= shell; dx++) {
                    // Only the surface of the shell.
                    int32_t chebyshev = dx > 0 ? dx : -dx;
                    int32_t ady = dy > 0 ? dy : -dy;
                    int32_t adz = dz > 0 ? dz : -dz;
                    if (ady > chebyshev) chebyshev = ady;
                    if (adz > chebyshev) chebyshev = adz;
                    if (chebyshev != shell) continue;

                    int32_t cx = center_cx + dx;
                    int32_t cy = center_cy + dy;
                    int32_t cz = center_cz + dz;
                    if (cx < 0 || cy < 0 || cz < 0) continue;

                    mlc_region r = {
                        .lod = lod,
                        .x0 = cx * MLC_CHUNK_DIM, .y0 = cy * MLC_CHUNK_DIM, .z0 = cz * MLC_CHUNK_DIM,
                        .x1 = (cx + 1) * MLC_CHUNK_DIM, .y1 = (cy + 1) * MLC_CHUNK_DIM, .z1 = (cz + 1) * MLC_CHUNK_DIM,
                    };
                    (void)mlc_prefetch_region(c, r, prio);
                }
            }
        }
    }

    return MLC_OK;
}

MLCDEF void mlc_cancel_prefetch(mlc_cache *c, mlc_priority min_priority) {
    if (!c) return;
    pthread_mutex_lock(&c->fetch.mu);
    mlc__pq_cancel_below(&c->fetch.pq, min_priority);
    pthread_mutex_unlock(&c->fetch.mu);
}

MLCDEF void mlc_set_viewpoint(mlc_cache *c, int32_t lod,
                               float vx, float vy, float vz) {
    if (!c) return;
    pthread_mutex_lock(&c->prefetch_lock);

    uint64_t now = mlc__now_us();
    mlc__prefetch_state *ps = &c->prefetch;

    if (ps->prev_time_us > 0) {
        double dt = (double)(now - ps->prev_time_us) / 1000000.0;
        if (dt > 0.001) { // avoid division by near-zero
            float alpha = 0.3f;
            for (int i = 0; i < 3; i++) {
                float raw_vel = ((&vx)[i] - ps->prev_viewpoint[i]) / (float)dt;
                ps->velocity[i] = ps->velocity[i] * (1.0f - alpha) + raw_vel * alpha;
            }
        }
    }

    ps->prev_viewpoint[0] = ps->viewpoint[0];
    ps->prev_viewpoint[1] = ps->viewpoint[1];
    ps->prev_viewpoint[2] = ps->viewpoint[2];
    ps->viewpoint[0] = vx;
    ps->viewpoint[1] = vy;
    ps->viewpoint[2] = vz;
    ps->prev_time_us = now;
    ps->current_lod = lod;

    pthread_mutex_unlock(&c->prefetch_lock);
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Progressive Rendering (stub for Phase 6)
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_status mlc_progressive_request(mlc_cache *c, mlc_region region,
                                           mlc_mini_ready_fn callback, void *userdata) {
    if (!c) return MLC_ERR_NULL_ARG;
    // Phase 6: implement LOD fallback + upgrade events.
    // For now, just prefetch at normal priority.
    (void)callback; (void)userdata;
    return mlc_prefetch_region(c, region, MLC_PRIORITY_NORMAL);
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Events
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_status mlc_subscribe(mlc_cache *c, mlc_event event, mlc_event_fn fn,
                                 void *userdata, uint64_t *sub_id_out) {
    if (!c || !fn) return MLC_ERR_NULL_ARG;
    return mlc__events_subscribe(&c->events, event, fn, userdata, sub_id_out);
}

MLCDEF void mlc_unsubscribe(mlc_cache *c, uint64_t sub_id) {
    if (!c) return;
    mlc__events_unsubscribe(&c->events, sub_id);
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Statistics
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_status mlc_get_stats(const mlc_cache *c, mlc_stats *out) {
    if (!c || !out) return MLC_ERR_NULL_ARG;
    memset(out, 0, sizeof(*out));

    for (int t = 0; t < MLC_TIER_COUNT; t++) {
        uint64_t h = atomic_load(&c->counters.hits[t]);
        uint64_t m = atomic_load(&c->counters.misses[t]);
        out->tier[t].hits = h;
        out->tier[t].misses = m;
        out->tier[t].evictions = atomic_load(&c->counters.evictions[t]);
        out->tier[t].hit_rate = (h + m > 0) ? (double)h / (double)(h + m) : 0.0;
        out->tier[t].avg_latency_us = mlc__latency_avg(&c->latency[t]);
        out->tier[t].p99_latency_us = mlc__latency_p99(&c->latency[t]);
    }

    out->tier[MLC_TIER_HOT].bytes_used = atomic_load(&c->hot_bytes);
    out->tier[MLC_TIER_HOT].bytes_budget = c->hot_budget;
    out->tier[MLC_TIER_HOT].items = c->hot.count;
    out->tier[MLC_TIER_WARM].bytes_used = atomic_load(&c->warm_bytes);
    out->tier[MLC_TIER_WARM].bytes_budget = c->warm_budget;
    out->tier[MLC_TIER_WARM].items = c->warm.count;

    out->pending_fetches = c->fetch.pq.count;
    out->deduplicated_requests = atomic_load(&c->counters.dedup_count);
    out->prefetch_hits = atomic_load(&c->counters.prefetch_used);
    out->total_requests = atomic_load(&c->counters.total_requests);

    uint64_t th = 0, tm = 0;
    for (int t = 0; t < MLC_TIER_COUNT; t++) {
        th += out->tier[t].hits;
        tm += out->tier[t].misses;
    }
    out->overall_hit_rate = (th + tm > 0) ? (double)th / (double)(th + tm) : 0.0;

    return MLC_OK;
}

MLCDEF mlc_status mlc_get_tier_stats(const mlc_cache *c, mlc_tier tier,
                                      mlc_tier_stats *out) {
    if (!c || !out) return MLC_ERR_NULL_ARG;
    if ((int)tier < 0 || (int)tier >= MLC_TIER_COUNT) return MLC_ERR_INVALID_CONFIG;
    mlc_stats full;
    mlc_get_stats(c, &full);
    *out = full.tier[tier];
    return MLC_OK;
}

MLCDEF void mlc_reset_stats(mlc_cache *c) {
    if (!c) return;
    memset(&c->counters, 0, sizeof(c->counters));
    memset(&c->latency, 0, sizeof(c->latency));
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Budget Control
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_status mlc_set_budget(mlc_cache *c, mlc_tier tier, size_t bytes) {
    if (!c) return MLC_ERR_NULL_ARG;
    switch (tier) {
    case MLC_TIER_HOT:
        c->hot_budget = bytes;
        // Trigger eviction if over new budget.
        pthread_rwlock_wrlock(&c->hot_lock);
        mlc__hot_evict(c);
        pthread_rwlock_unlock(&c->hot_lock);
        return MLC_OK;
    case MLC_TIER_WARM:
        c->warm_budget = bytes;
        pthread_rwlock_wrlock(&c->warm_lock);
        mlc__warm_evict(c);
        pthread_rwlock_unlock(&c->warm_lock);
        return MLC_OK;
    default:
        return MLC_ERR_UNSUPPORTED;
    }
}

MLCDEF size_t mlc_get_budget(const mlc_cache *c, mlc_tier tier) {
    if (!c) return 0;
    switch (tier) {
    case MLC_TIER_HOT:  return c->hot_budget;
    case MLC_TIER_WARM: return c->warm_budget;
    default:            return 0;
    }
}

MLCDEF mlc_status mlc_evict(mlc_cache *c, mlc_tier tier, float fraction) {
    if (!c) return MLC_ERR_NULL_ARG;
    if (fraction < 0.0f || fraction > 1.0f) return MLC_ERR_INVALID_CONFIG;

    switch (tier) {
    case MLC_TIER_HOT: {
        size_t current = c->hot_budget;
        size_t temp_budget = (size_t)((double)current * (1.0 - (double)fraction));
        c->hot_budget = temp_budget;
        pthread_rwlock_wrlock(&c->hot_lock);
        mlc__hot_evict(c);
        pthread_rwlock_unlock(&c->hot_lock);
        c->hot_budget = current; // restore
        return MLC_OK;
    }
    case MLC_TIER_WARM: {
        size_t current = c->warm_budget;
        size_t temp_budget = (size_t)((double)current * (1.0 - (double)fraction));
        c->warm_budget = temp_budget;
        pthread_rwlock_wrlock(&c->warm_lock);
        mlc__warm_evict(c);
        pthread_rwlock_unlock(&c->warm_lock);
        c->warm_budget = current;
        return MLC_OK;
    }
    default:
        return MLC_ERR_UNSUPPORTED;
    }
}

MLCDEF void mlc_flush(mlc_cache *c) {
    if (!c) return;

    // Cancel all pending fetches.
    pthread_mutex_lock(&c->fetch.mu);
    c->fetch.pq.count = 0;
    pthread_mutex_unlock(&c->fetch.mu);

    // Force-clear HOT tier (bypass CLOCK heuristics).
    pthread_rwlock_wrlock(&c->hot_lock);
    for (uint32_t i = 0; i < c->hot.cap; i++) {
        mlc__hot_slot *s = &c->hot.slots[i];
        if (s->occupied) {
            if (s->val.data) MLC_FREE(s->val.data);
            s->occupied = false;
        }
    }
    c->hot.count = 0;
    atomic_store(&c->hot_bytes, 0);
    pthread_rwlock_unlock(&c->hot_lock);

    // Force-clear WARM tier.
    pthread_rwlock_wrlock(&c->warm_lock);
    for (uint32_t i = 0; i < c->warm.cap; i++) {
        mlc__warm_slot *s = &c->warm.slots[i];
        if (s->occupied) {
            if (s->val.h264_data) MLC_FREE(s->val.h264_data);
            s->occupied = false;
        }
    }
    c->warm.count = 0;
    atomic_store(&c->warm_bytes, 0);
    pthread_rwlock_unlock(&c->warm_lock);
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Codec Registration
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF void mlc_set_decoder(mlc_cache *c, mlc_decode_fn fn, void *userdata) {
    if (!c) return;
    c->decode_fn = fn;
    c->decode_userdata = userdata;
}

MLCDEF void mlc_set_encoder(mlc_cache *c, mlc_encode_fn fn, void *userdata) {
    if (!c) return;
    c->encode_fn = fn;
    c->encode_userdata = userdata;
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Data Injection
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF mlc_status mlc_inject_chunk(mlc_cache *c, mlc_chunk_key key,
                                    const uint8_t *decoded_data) {
    if (!c || !decoded_data) return MLC_ERR_NULL_ARG;
    if (atomic_load(&c->shutting_down)) return MLC_ERR_SHUTDOWN;
    return mlc__decode_chunk_to_hot(c, key, decoded_data);
}

MLCDEF mlc_status mlc_inject_compressed(mlc_cache *c, mlc_chunk_key key,
                                         const void *compressed, size_t len) {
    if (!c || !compressed || len == 0) return MLC_ERR_NULL_ARG;
    if (atomic_load(&c->shutting_down)) return MLC_ERR_SHUTDOWN;

    // Copy data and promote to WARM.
    uint8_t *copy = (uint8_t *)MLC_MALLOC(len);
    if (!copy) return MLC_ERR_ALLOC;
    memcpy(copy, compressed, len);
    return mlc__promote_warm(c, key, copy, (uint32_t)len);
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — Scroll Hint
// ═══════════════════════════════════════════════════════════════════════════

MLCDEF void mlc_hint_scroll(mlc_cache *c, int32_t lod,
                             int axis, float position, float velocity) {
    if (!c) return;
    if (axis < 0 || axis > 2) return;
    pthread_mutex_lock(&c->prefetch_lock);
    c->scroll_hint.lod = lod;
    c->scroll_hint.axis = axis;
    c->scroll_hint.position = position;
    c->scroll_hint.velocity = velocity;
    pthread_mutex_unlock(&c->prefetch_lock);

    // Generate prefetch requests ahead of scroll direction.
    if (velocity == 0.0f) return;

    float lookahead_slices = velocity * 0.5f; // 500ms ahead
    int32_t ahead = (int32_t)(lookahead_slices > 0 ? lookahead_slices : -lookahead_slices);
    if (ahead < 1) ahead = 1;
    if (ahead > 32) ahead = 32; // cap

    int32_t pos_chunk = (int32_t)position / MLC_CHUNK_DIM;
    int32_t dir = velocity > 0 ? 1 : -1;

    for (int32_t i = 1; i <= ahead; i++) {
        int32_t target = pos_chunk + dir * i;
        if (target < 0) continue;

        mlc_chunk_key ck = { .lod = lod, .cx = 0, .cy = 0, .cz = 0 };
        // Set the chunk coord along the scroll axis.
        switch (axis) {
        case 0: ck.cz = target; break;
        case 1: ck.cy = target; break;
        case 2: ck.cx = target; break;
        }
        // Prefetch this chunk with decreasing priority.
        mlc_priority prio = (i <= 2) ? MLC_PRIORITY_HIGH :
                            (i <= 8) ? MLC_PRIORITY_NORMAL : MLC_PRIORITY_LOW;
        mlc_region r = {
            .lod = lod,
            .x0 = ck.cx * MLC_CHUNK_DIM,
            .y0 = ck.cy * MLC_CHUNK_DIM,
            .z0 = ck.cz * MLC_CHUNK_DIM,
            .x1 = (ck.cx + 1) * MLC_CHUNK_DIM,
            .y1 = (ck.cy + 1) * MLC_CHUNK_DIM,
            .z1 = (ck.cz + 1) * MLC_CHUNK_DIM,
        };
        (void)mlc_prefetch_region(c, r, prio);
    }
}
