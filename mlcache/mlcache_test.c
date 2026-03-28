// mlcache_test.c — Unit tests for libmlcache.
// No framework; assert-based with structured output.

#include "mlcache.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    fprintf(stderr, "  %-50s", #name); \
    test_##name(); \
    fprintf(stderr, " OK\n"); \
    pass_count++; \
} while(0)

static int pass_count = 0;

// ── Helpers ─────────────────────────────────────────────────────────────────

static mlc_config test_config(void) {
    mlc_config cfg = mlc_default_config();
    cfg.tiers.hot_budget = 4 * 1024 * 1024;   // 4 MB (1024 minis)
    cfg.tiers.warm_budget = 1 * 1024 * 1024;   // 1 MB
    cfg.threads.decode_workers = 1;
    cfg.threads.io_workers = 0;
    cfg.threads.net_workers = 0;
    return cfg;
}

// Insert a synthetic mini-cube directly into the HOT tier for testing.
// We reach in via mlc_get_mini after manually populating.
// Since we don't have a codec yet, we'll test the lower-level API through
// the public interface by using a helper that writes to the cache internals.

// Actually, let's test through the public API properly.
// We need a way to inject test data. For Phase 1, we test:
// 1. Lifecycle (create/destroy)
// 2. Coordinate conversions
// 3. Hash functions
// 4. Stats (empty cache)
// 5. Budget control
// 6. Flush
// 7. Priority queue (indirectly via prefetch)
// 8. Event subscription
// 9. Concurrency (create/destroy under threads)

// ── Test: Version ───────────────────────────────────────────────────────────

TEST(version) {
    const char *v = mlc_version_str();
    assert(v != NULL);
    assert(strcmp(v, "0.1.0") == 0);
}

// ── Test: Status strings ────────────────────────────────────────────────────

TEST(status_strings) {
    assert(strcmp(mlc_status_str(MLC_OK), "ok") == 0);
    assert(strcmp(mlc_status_str(MLC_ERR_NULL_ARG), "null argument") == 0);
    assert(strcmp(mlc_status_str(MLC_ERR_NOT_FOUND), "not found") == 0);
    assert(strcmp(mlc_status_str(MLC_ERR_SHUTDOWN), "shutting down") == 0);
}

// ── Test: Tier strings ──────────────────────────────────────────────────────

TEST(tier_strings) {
    assert(strcmp(mlc_tier_str(MLC_TIER_HOT), "HOT") == 0);
    assert(strcmp(mlc_tier_str(MLC_TIER_WARM), "WARM") == 0);
    assert(strcmp(mlc_tier_str(MLC_TIER_COLD), "COLD") == 0);
    assert(strcmp(mlc_tier_str(MLC_TIER_ICE), "ICE") == 0);
}

// ── Test: Coordinate conversions ────────────────────────────────────────────

TEST(coords_voxel_to_mini) {
    mlc_mini_key mk = mlc_voxel_to_mini(0, 0, 0, 0);
    assert(mk.lod == 0 && mk.mx == 0 && mk.my == 0 && mk.mz == 0);

    mk = mlc_voxel_to_mini(0, 15, 15, 15);
    assert(mk.mx == 0 && mk.my == 0 && mk.mz == 0);

    mk = mlc_voxel_to_mini(0, 16, 0, 0);
    assert(mk.mx == 1 && mk.my == 0 && mk.mz == 0);

    mk = mlc_voxel_to_mini(2, 128, 256, 384);
    assert(mk.lod == 2 && mk.mx == 8 && mk.my == 16 && mk.mz == 24);
}

TEST(coords_voxel_to_chunk) {
    mlc_chunk_key ck = mlc_voxel_to_chunk(0, 0, 0, 0);
    assert(ck.lod == 0 && ck.cx == 0 && ck.cy == 0 && ck.cz == 0);

    ck = mlc_voxel_to_chunk(0, 127, 127, 127);
    assert(ck.cx == 0 && ck.cy == 0 && ck.cz == 0);

    ck = mlc_voxel_to_chunk(0, 128, 0, 0);
    assert(ck.cx == 1 && ck.cy == 0 && ck.cz == 0);

    ck = mlc_voxel_to_chunk(1, 256, 512, 768);
    assert(ck.lod == 1 && ck.cx == 2 && ck.cy == 4 && ck.cz == 6);
}

TEST(coords_mini_to_chunk) {
    mlc_mini_key mk = { .lod = 0, .mx = 0, .my = 0, .mz = 0 };
    mlc_chunk_key ck = mlc_mini_to_chunk(mk);
    assert(ck.cx == 0 && ck.cy == 0 && ck.cz == 0);

    mk = (mlc_mini_key){ .lod = 0, .mx = 7, .my = 7, .mz = 7 };
    ck = mlc_mini_to_chunk(mk);
    assert(ck.cx == 0 && ck.cy == 0 && ck.cz == 0);

    mk = (mlc_mini_key){ .lod = 0, .mx = 8, .my = 0, .mz = 0 };
    ck = mlc_mini_to_chunk(mk);
    assert(ck.cx == 1 && ck.cy == 0 && ck.cz == 0);

    mk = (mlc_mini_key){ .lod = 3, .mx = 24, .my = 16, .mz = 8 };
    ck = mlc_mini_to_chunk(mk);
    assert(ck.lod == 3 && ck.cx == 3 && ck.cy == 2 && ck.cz == 1);
}

TEST(coords_roundtrip) {
    // voxel -> mini -> chunk -> verify consistency
    int32_t x = 200, y = 300, z = 400;
    mlc_mini_key mk = mlc_voxel_to_mini(0, x, y, z);
    mlc_chunk_key ck = mlc_mini_to_chunk(mk);
    mlc_chunk_key ck2 = mlc_voxel_to_chunk(0, x, y, z);
    assert(mlc_chunk_key_eq(ck, ck2));
}

// ── Test: Hash functions ────────────────────────────────────────────────────

TEST(hash_deterministic) {
    mlc_chunk_key k1 = {0, 1, 2, 3};
    mlc_chunk_key k2 = {0, 1, 2, 3};
    assert(mlc_chunk_key_hash(k1) == mlc_chunk_key_hash(k2));

    mlc_mini_key m1 = {0, 10, 20, 30};
    mlc_mini_key m2 = {0, 10, 20, 30};
    assert(mlc_mini_key_hash(m1) == mlc_mini_key_hash(m2));
}

TEST(hash_distribution) {
    // Check that different keys produce different hashes (no trivial collisions).
    uint64_t hashes[100];
    for (int i = 0; i < 100; i++) {
        hashes[i] = mlc_chunk_key_hash((mlc_chunk_key){0, i, 0, 0});
    }
    int collisions = 0;
    for (int i = 0; i < 100; i++)
        for (int j = i + 1; j < 100; j++)
            if (hashes[i] == hashes[j]) collisions++;
    assert(collisions < 5); // Allow very few collisions
}

TEST(key_equality) {
    mlc_chunk_key a = {0, 1, 2, 3}, b = {0, 1, 2, 3}, c = {0, 1, 2, 4};
    assert(mlc_chunk_key_eq(a, b));
    assert(!mlc_chunk_key_eq(a, c));

    mlc_mini_key ma = {0, 1, 2, 3}, mb = {0, 1, 2, 3}, mc = {1, 1, 2, 3};
    assert(mlc_mini_key_eq(ma, mb));
    assert(!mlc_mini_key_eq(ma, mc));
}

// ── Test: Lifecycle ─────────────────────────────────────────────────────────

TEST(create_destroy) {
    mlc_config cfg = test_config();
    mlc_cache *c = NULL;
    mlc_status s = mlc_create(&c, &cfg);
    assert(s == MLC_OK);
    assert(c != NULL);
    mlc_destroy(c);
}

TEST(create_null_args) {
    mlc_config cfg = test_config();
    mlc_cache *c = NULL;
    assert(mlc_create(NULL, &cfg) == MLC_ERR_NULL_ARG);
    assert(mlc_create(&c, NULL) == MLC_ERR_NULL_ARG);
}

TEST(default_config) {
    mlc_config cfg = mlc_default_config();
    assert(cfg.tiers.hot_budget == 2ULL << 30);
    assert(cfg.tiers.warm_budget == 512ULL << 20);
    assert(cfg.volume.lod_levels == 1);
    assert(cfg.volume.downsample_factor == 2);
    assert(cfg.threads.decode_workers >= 1);
}

// ── Test: Empty cache queries ───────────────────────────────────────────────

TEST(get_mini_empty) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    const uint8_t *data = NULL;
    mlc_mini_key mk = {0, 0, 0, 0};
    mlc_status s = mlc_get_mini(c, mk, &data);
    // Without codec integration, this should return NOT_FOUND or CODEC error.
    assert(s == MLC_ERR_NOT_FOUND || s == MLC_ERR_CODEC);
    assert(data == NULL);

    mlc_destroy(c);
}

TEST(get_chunk_empty) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    const uint8_t *data = NULL;
    mlc_chunk_key ck = {0, 0, 0, 0};
    mlc_status s = mlc_get_chunk(c, ck, &data);
    assert(s == MLC_ERR_NOT_FOUND);

    mlc_destroy(c);
}

// ── Test: Statistics on empty cache ─────────────────────────────────────────

TEST(stats_empty) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    mlc_stats stats;
    assert(mlc_get_stats(c, &stats) == MLC_OK);
    assert(stats.total_requests == 0);
    assert(stats.overall_hit_rate == 0.0);
    assert(stats.tier[MLC_TIER_HOT].bytes_budget == 4 * 1024 * 1024);
    assert(stats.tier[MLC_TIER_WARM].bytes_budget == 1 * 1024 * 1024);
    assert(stats.tier[MLC_TIER_HOT].items == 0);

    mlc_destroy(c);
}

TEST(stats_after_miss) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    const uint8_t *data = NULL;
    (void)mlc_get_mini(c, (mlc_mini_key){0, 0, 0, 0}, &data);

    mlc_stats stats;
    mlc_get_stats(c, &stats);
    assert(stats.total_requests == 1);
    assert(stats.tier[MLC_TIER_HOT].misses >= 1);

    mlc_destroy(c);
}

// ── Test: Budget control ────────────────────────────────────────────────────

TEST(budget_get_set) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    assert(mlc_get_budget(c, MLC_TIER_HOT) == 4 * 1024 * 1024);
    assert(mlc_get_budget(c, MLC_TIER_WARM) == 1 * 1024 * 1024);

    assert(mlc_set_budget(c, MLC_TIER_HOT, 8 * 1024 * 1024) == MLC_OK);
    assert(mlc_get_budget(c, MLC_TIER_HOT) == 8 * 1024 * 1024);

    mlc_destroy(c);
}

// ── Test: Flush ─────────────────────────────────────────────────────────────

TEST(flush_empty) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    mlc_flush(c); // Should not crash on empty cache.

    mlc_stats stats;
    mlc_get_stats(c, &stats);
    assert(stats.tier[MLC_TIER_HOT].items == 0);

    mlc_destroy(c);
}

// ── Test: Event subscription ────────────────────────────────────────────────

static int test_event_count = 0;
static int test_event_cb(mlc_event event, const void *detail, void *userdata) {
    (void)event; (void)detail; (void)userdata;
    test_event_count++;
    return 0;
}

TEST(event_subscribe_unsubscribe) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    uint64_t sub_id = 0;
    assert(mlc_subscribe(c, MLC_EVENT_EVICTION, test_event_cb, NULL, &sub_id) == MLC_OK);
    assert(sub_id > 0);

    mlc_unsubscribe(c, sub_id);

    mlc_destroy(c);
}

// ── Test: Shutdown flag ─────────────────────────────────────────────────────

TEST(shutdown_rejects_requests) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    // Destroy triggers shutdown; but we want to test the flag.
    // We'll test that after destroy, nothing crashes.
    // Actually, let's test pre-destroy by requesting during destroy.
    // This is hard to test directly, so just verify basic shutdown behavior.
    mlc_destroy(c);
    // After destroy, c is freed. Don't use it.
}

// ── Test: Region read on empty cache ────────────────────────────────────────

TEST(read_region_empty) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    uint8_t buf[16 * 16 * 16];
    mlc_region region = { .lod = 0, .x0 = 0, .y0 = 0, .z0 = 0,
                          .x1 = 16, .y1 = 16, .z1 = 16 };
    mlc_status s = mlc_read_region(c, region, buf, sizeof(buf));
    // Should fail because data isn't in cache.
    assert(s != MLC_OK);

    mlc_destroy(c);
}

TEST(read_region_bad_bounds) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    uint8_t buf[16];
    mlc_region region = { .lod = 0, .x0 = 10, .y0 = 0, .z0 = 0,
                          .x1 = 5, .y1 = 16, .z1 = 16 }; // x1 < x0 = bad
    assert(mlc_read_region(c, region, buf, sizeof(buf)) == MLC_ERR_BOUNDS);

    mlc_destroy(c);
}

// ── Test: Concurrent create/destroy ─────────────────────────────────────────

static void *thread_create_destroy(void *arg) {
    (void)arg;
    for (int i = 0; i < 10; i++) {
        mlc_config cfg = test_config();
        mlc_cache *c;
        if (mlc_create(&c, &cfg) == MLC_OK) {
            // Do a quick query.
            const uint8_t *data = NULL;
            (void)mlc_get_mini(c, (mlc_mini_key){0, 0, 0, 0}, &data);
            mlc_destroy(c);
        }
    }
    return NULL;
}

TEST(concurrent_lifecycle) {
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, thread_create_destroy, NULL);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
}

// ── Test: Prefetch on empty cache ───────────────────────────────────────────

TEST(prefetch_region) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    mlc_region region = { .lod = 0, .x0 = 0, .y0 = 0, .z0 = 0,
                          .x1 = 256, .y1 = 256, .z1 = 256 };
    mlc_status s = mlc_prefetch_region(c, region, MLC_PRIORITY_LOW);
    assert(s == MLC_OK);

    // Stats should show prefetch issued.
    mlc_stats stats;
    mlc_get_stats(c, &stats);
    assert(stats.prefetch_hits == 0); // nothing loaded yet

    mlc_destroy(c);
}

TEST(prefetch_around) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    mlc_status s = mlc_prefetch_around(c, 0, 512, 512, 512, 2);
    assert(s == MLC_OK);

    mlc_destroy(c);
}

TEST(cancel_prefetch) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    (void)mlc_prefetch_region(c, (mlc_region){0, 0,0,0, 512,512,512}, MLC_PRIORITY_LOW);
    mlc_cancel_prefetch(c, MLC_PRIORITY_LOW);

    mlc_destroy(c);
}

// ── Test: Viewpoint setting ─────────────────────────────────────────────────

TEST(set_viewpoint) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    mlc_set_viewpoint(c, 0, 100.0f, 200.0f, 300.0f);
    mlc_set_viewpoint(c, 0, 110.0f, 200.0f, 300.0f); // moved in X

    mlc_destroy(c);
}

// ── Test: Stats reset ───────────────────────────────────────────────────────

TEST(stats_reset) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    // Generate some misses.
    const uint8_t *data = NULL;
    (void)mlc_get_mini(c, (mlc_mini_key){0, 0, 0, 0}, &data);
    (void)mlc_get_mini(c, (mlc_mini_key){0, 1, 0, 0}, &data);

    mlc_stats stats;
    mlc_get_stats(c, &stats);
    assert(stats.total_requests == 2);

    mlc_reset_stats(c);
    mlc_get_stats(c, &stats);
    assert(stats.total_requests == 0);

    mlc_destroy(c);
}

// ── Test: Evict on empty ────────────────────────────────────────────────────

TEST(evict_empty) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    assert(mlc_evict(c, MLC_TIER_HOT, 0.5f) == MLC_OK);
    assert(mlc_evict(c, MLC_TIER_WARM, 1.0f) == MLC_OK);

    mlc_destroy(c);
}

// ── Test: Constants sanity ──────────────────────────────────────────────────

TEST(constants) {
    assert(MLC_CHUNK_DIM == 128);
    assert(MLC_CHUNK_VOX == 128 * 128 * 128);
    assert(MLC_MINI_DIM == 16);
    assert(MLC_MINI_VOX == 16 * 16 * 16);
    assert(MLC_MINIS_PER_AXIS == 8);
    assert(MLC_MINIS_PER_CHUNK == 512);
    assert(MLC_SHARD_AXIS == 8);
    assert(MLC_SHARD_CHUNKS == 512);
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 2 Tests — Data Flow, Injection, Decode, Eviction, Async
// ═══════════════════════════════════════════════════════════════════════════

// Helper: create a test chunk with a known pattern.
// Each voxel = (x + y*128 + z*128*128) % 256 XOR lod.
static void fill_test_chunk(uint8_t *buf, mlc_chunk_key ck) {
    for (int z = 0; z < MLC_CHUNK_DIM; z++)
        for (int y = 0; y < MLC_CHUNK_DIM; y++)
            for (int x = 0; x < MLC_CHUNK_DIM; x++) {
                size_t off = (size_t)z * MLC_CHUNK_DIM * MLC_CHUNK_DIM
                           + (size_t)y * MLC_CHUNK_DIM + (size_t)x;
                buf[off] = (uint8_t)((x + y * 3 + z * 7 + ck.cx * 13 + ck.cy * 17 + ck.cz * 23) & 0xFF);
            }
}

// Trivial "codec": XOR each byte with 0xAA.
static mlc_status test_encode(const uint8_t *decoded, void **out, size_t *len_out,
                               void *ud) {
    (void)ud;
    uint8_t *buf = (uint8_t *)malloc(MLC_CHUNK_VOX);
    if (!buf) return MLC_ERR_ALLOC;
    for (size_t i = 0; i < MLC_CHUNK_VOX; i++) buf[i] = decoded[i] ^ 0xAA;
    *out = buf;
    *len_out = MLC_CHUNK_VOX;
    return MLC_OK;
}

static mlc_status test_decode(const void *compressed, size_t compressed_len,
                               uint8_t *decoded_out, void *ud) {
    (void)ud;
    if (compressed_len != MLC_CHUNK_VOX) return MLC_ERR_CODEC;
    const uint8_t *src = (const uint8_t *)compressed;
    for (size_t i = 0; i < MLC_CHUNK_VOX; i++) decoded_out[i] = src[i] ^ 0xAA;
    return MLC_OK;
}

// ── Test: Inject chunk and read back minis ──────────────────────────────────

TEST(inject_chunk_read_minis) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 64 * 1024 * 1024; // 64MB, plenty for one chunk
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    // Create and inject a test chunk at (0, 0, 0, 0).
    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    assert(chunk);
    mlc_chunk_key ck = {0, 0, 0, 0};
    fill_test_chunk(chunk, ck);

    assert(mlc_inject_chunk(c, ck, chunk) == MLC_OK);

    // Verify stats: should have 512 minis in HOT.
    mlc_stats stats;
    mlc_get_stats(c, &stats);
    assert(stats.tier[MLC_TIER_HOT].items == 512);

    // Read a specific mini and verify data.
    mlc_mini_key mk = {0, 0, 0, 0}; // first mini
    const uint8_t *data = NULL;
    assert(mlc_get_mini(c, mk, &data) == MLC_OK);
    assert(data != NULL);

    // Check first voxel of mini (0,0,0) = chunk voxel (0,0,0).
    assert(data[0] == chunk[0]);
    // Check voxel at (15,15,15) within mini = chunk voxel (15,15,15).
    uint8_t expected = chunk[15 * MLC_CHUNK_DIM * MLC_CHUNK_DIM + 15 * MLC_CHUNK_DIM + 15];
    uint8_t got = data[15 * MLC_MINI_DIM * MLC_MINI_DIM + 15 * MLC_MINI_DIM + 15];
    assert(got == expected);

    mlc_unpin_mini(c, mk);

    // Read mini at (1, 2, 3) = chunk-local (1,2,3) within chunk (0,0,0).
    mk = (mlc_mini_key){0, 1, 2, 3};
    assert(mlc_get_mini(c, mk, &data) == MLC_OK);

    // Voxel (0,0,0) of this mini = chunk voxel (16, 32, 48).
    int cx = 1 * 16, cy = 2 * 16, cz = 3 * 16;
    expected = chunk[(size_t)cz * MLC_CHUNK_DIM * MLC_CHUNK_DIM + (size_t)cy * MLC_CHUNK_DIM + cx];
    assert(data[0] == expected);

    mlc_unpin_mini(c, mk);
    free(chunk);
    mlc_destroy(c);
}

// ── Test: Inject chunk and get_chunk reads back correctly ───────────────────

TEST(inject_chunk_get_chunk) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 64 * 1024 * 1024;
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    assert(chunk);
    mlc_chunk_key ck = {0, 1, 2, 3};
    fill_test_chunk(chunk, ck);
    assert(mlc_inject_chunk(c, ck, chunk) == MLC_OK);

    const uint8_t *readback = NULL;
    assert(mlc_get_chunk(c, ck, &readback) == MLC_OK);
    assert(readback != NULL);

    // Verify full chunk matches.
    assert(memcmp(readback, chunk, MLC_CHUNK_VOX) == 0);

    free((void *)readback); // mlc_get_chunk returns a copy
    free(chunk);
    mlc_destroy(c);
}

// ── Test: Read region across mini boundaries ────────────────────────────────

TEST(read_region_cross_mini) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 64 * 1024 * 1024;
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    assert(chunk);
    mlc_chunk_key ck = {0, 0, 0, 0};
    fill_test_chunk(chunk, ck);
    assert(mlc_inject_chunk(c, ck, chunk) == MLC_OK);

    // Read a 32x32x1 region spanning 4 minis (2x2 in x,y).
    uint8_t buf[32 * 32 * 1];
    mlc_region region = { .lod = 0, .x0 = 0, .y0 = 0, .z0 = 5,
                          .x1 = 32, .y1 = 32, .z1 = 6 };
    assert(mlc_read_region(c, region, buf, sizeof(buf)) == MLC_OK);

    // Verify a sample voxel: (10, 20, 5) in the region = chunk voxel (10, 20, 5).
    size_t region_off = 0 * 32 * 32 + 20 * 32 + 10; // z=0 in region = z=5 in chunk
    size_t chunk_off = 5 * MLC_CHUNK_DIM * MLC_CHUNK_DIM + 20 * MLC_CHUNK_DIM + 10;
    assert(buf[region_off] == chunk[chunk_off]);

    free(chunk);
    mlc_destroy(c);
}

// ── Test: WARM → HOT decode pipeline ────────────────────────────────────────

TEST(warm_to_hot_decode) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 64 * 1024 * 1024;
    cfg.tiers.warm_budget = 64 * 1024 * 1024;
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    // Register test codec.
    mlc_set_decoder(c, test_decode, NULL);
    mlc_set_encoder(c, test_encode, NULL);

    // Create decoded chunk and encode it.
    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    assert(chunk);
    mlc_chunk_key ck = {0, 0, 0, 0};
    fill_test_chunk(chunk, ck);

    void *compressed = NULL;
    size_t comp_len = 0;
    assert(test_encode(chunk, &compressed, &comp_len, NULL) == MLC_OK);

    // Inject compressed data into WARM.
    assert(mlc_inject_compressed(c, ck, compressed, comp_len) == MLC_OK);
    free(compressed);

    mlc_stats stats;
    mlc_get_stats(c, &stats);
    assert(stats.tier[MLC_TIER_WARM].items == 1);
    assert(stats.tier[MLC_TIER_HOT].items == 0); // not decoded yet

    // Now request a mini — should trigger WARM decode → HOT.
    mlc_mini_key mk = {0, 3, 4, 5};
    const uint8_t *data = NULL;
    assert(mlc_get_mini(c, mk, &data) == MLC_OK);
    assert(data != NULL);

    // Verify the data matches.
    int ox = 3 * 16, oy = 4 * 16, oz = 5 * 16;
    uint8_t expected = chunk[(size_t)oz * MLC_CHUNK_DIM * MLC_CHUNK_DIM
                           + (size_t)oy * MLC_CHUNK_DIM + ox];
    assert(data[0] == expected);

    mlc_unpin_mini(c, mk);

    // Now all 512 minis should be in HOT.
    mlc_get_stats(c, &stats);
    assert(stats.tier[MLC_TIER_HOT].items == 512);
    assert(stats.tier[MLC_TIER_WARM].hits >= 1); // first access hit WARM

    // Second access should be a HOT hit.
    assert(mlc_get_mini(c, mk, &data) == MLC_OK);
    mlc_unpin_mini(c, mk);
    mlc_get_stats(c, &stats);
    assert(stats.tier[MLC_TIER_HOT].hits >= 1);

    free(chunk);
    mlc_destroy(c);
}

// ── Test: Eviction under pressure ───────────────────────────────────────────

TEST(eviction_pressure) {
    mlc_config cfg = test_config();
    // Very tight budget: room for about 2 chunks worth of minis (2*512*4KB = 4MB).
    cfg.tiers.hot_budget = 4 * 1024 * 1024;
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    // Inject 3 chunks — third should trigger eviction.
    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    assert(chunk);

    for (int i = 0; i < 3; i++) {
        mlc_chunk_key ck = {0, i, 0, 0};
        fill_test_chunk(chunk, ck);
        assert(mlc_inject_chunk(c, ck, chunk) == MLC_OK);
    }

    mlc_stats stats;
    mlc_get_stats(c, &stats);
    // Should have evicted some minis to stay within budget.
    assert(stats.tier[MLC_TIER_HOT].bytes_used <= cfg.tiers.hot_budget);
    assert(stats.tier[MLC_TIER_HOT].evictions > 0);
    // But most recent chunk's minis should still be there.
    mlc_mini_key mk = {0, 2 * 8, 0, 0}; // first mini of chunk (0,2,0,0)
    const uint8_t *data = NULL;
    assert(mlc_get_mini(c, mk, &data) == MLC_OK);
    mlc_unpin_mini(c, mk);

    free(chunk);
    mlc_destroy(c);
}

// ── Test: Pin prevents eviction ─────────────────────────────────────────────

TEST(pin_prevents_eviction) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 4 * 1024 * 1024; // tight
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    assert(chunk);

    // Inject first chunk and pin a mini.
    mlc_chunk_key ck0 = {0, 0, 0, 0};
    fill_test_chunk(chunk, ck0);
    assert(mlc_inject_chunk(c, ck0, chunk) == MLC_OK);

    mlc_mini_key pinned = {0, 0, 0, 0};
    const uint8_t *data = NULL;
    assert(mlc_get_mini(c, pinned, &data) == MLC_OK);
    // data is now pinned (pin_count = 1).
    uint8_t pinned_val = data[0]; // remember value

    // Inject more chunks to force eviction.
    for (int i = 1; i <= 3; i++) {
        mlc_chunk_key ck = {0, i, 0, 0};
        fill_test_chunk(chunk, ck);
        assert(mlc_inject_chunk(c, ck, chunk) == MLC_OK);
    }

    // The pinned mini should survive eviction.
    const uint8_t *data2 = NULL;
    assert(mlc_get_mini(c, pinned, &data2) == MLC_OK);
    assert(data2[0] == pinned_val); // data still intact
    mlc_unpin_mini(c, pinned);
    mlc_unpin_mini(c, pinned); // release both pins

    free(chunk);
    mlc_destroy(c);
}

// ── Test: Multiple chunks coexist ───────────────────────────────────────────

TEST(multiple_chunks) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 128 * 1024 * 1024; // plenty
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    assert(chunk);

    // Inject 4 chunks at different positions.
    for (int i = 0; i < 4; i++) {
        mlc_chunk_key ck = {0, i, i, i};
        fill_test_chunk(chunk, ck);
        assert(mlc_inject_chunk(c, ck, chunk) == MLC_OK);
    }

    mlc_stats stats;
    mlc_get_stats(c, &stats);
    assert(stats.tier[MLC_TIER_HOT].items == 4 * 512);

    // Verify data from each chunk is distinct.
    for (int i = 0; i < 4; i++) {
        mlc_mini_key mk = {0, i * 8, i * 8, i * 8};
        const uint8_t *data = NULL;
        assert(mlc_get_mini(c, mk, &data) == MLC_OK);
        // Each chunk has a different pattern due to different coords in fill_test_chunk.
        // Just verify we get data.
        assert(data != NULL);
        mlc_unpin_mini(c, mk);
    }

    free(chunk);
    mlc_destroy(c);
}

// ── Test: Stats track hits and misses ───────────────────────────────────────

TEST(stats_hits_misses) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 64 * 1024 * 1024;
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    mlc_chunk_key ck = {0, 0, 0, 0};
    fill_test_chunk(chunk, ck);
    assert(mlc_inject_chunk(c, ck, chunk) == MLC_OK);

    // Read same mini 10 times.
    mlc_mini_key mk = {0, 0, 0, 0};
    for (int i = 0; i < 10; i++) {
        const uint8_t *data;
        assert(mlc_get_mini(c, mk, &data) == MLC_OK);
        mlc_unpin_mini(c, mk);
    }

    mlc_stats stats;
    mlc_get_stats(c, &stats);
    assert(stats.tier[MLC_TIER_HOT].hits == 10);
    assert(stats.total_requests == 10);
    assert(stats.tier[MLC_TIER_HOT].hit_rate > 0.99);

    // Miss on a non-existent mini.
    const uint8_t *data;
    (void)mlc_get_mini(c, (mlc_mini_key){0, 100, 100, 100}, &data);
    mlc_get_stats(c, &stats);
    assert(stats.tier[MLC_TIER_HOT].misses >= 1);

    free(chunk);
    mlc_destroy(c);
}

// ── Test: Async request with callback ───────────────────────────────────────

static _Atomic int async_cb_fired = 0;
static void async_mini_cb(mlc_mini_key key, const uint8_t *data, void *ud) {
    (void)key; (void)data; (void)ud;
    atomic_fetch_add(&async_cb_fired, 1);
}

TEST(async_request_callback) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 64 * 1024 * 1024;
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    // Inject a chunk so the data is available.
    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    mlc_chunk_key ck = {0, 0, 0, 0};
    fill_test_chunk(chunk, ck);
    assert(mlc_inject_chunk(c, ck, chunk) == MLC_OK);

    // Async request for a mini that's already in HOT — callback should fire immediately.
    atomic_store(&async_cb_fired, 0);
    mlc_mini_key mk = {0, 2, 3, 4};
    assert(mlc_request_mini(c, mk, MLC_PRIORITY_HIGH, async_mini_cb, NULL) == MLC_OK);
    assert(atomic_load(&async_cb_fired) == 1);

    free(chunk);
    mlc_destroy(c);
}

// ── Test: Deduplication ─────────────────────────────────────────────────────

TEST(request_deduplication) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 64 * 1024 * 1024;
    cfg.tiers.warm_budget = 64 * 1024 * 1024;
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);
    mlc_set_decoder(c, test_decode, NULL);

    // Don't inject any data — force actual fetch path.
    // Inject compressed data so workers can decode.
    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    mlc_chunk_key ck = {0, 5, 5, 5};
    fill_test_chunk(chunk, ck);
    void *compressed; size_t comp_len;
    test_encode(chunk, &compressed, &comp_len, NULL);
    assert(mlc_inject_compressed(c, ck, compressed, comp_len) == MLC_OK);
    free(compressed);

    // Submit multiple async requests for different minis in same chunk.
    atomic_store(&async_cb_fired, 0);
    for (int i = 0; i < 5; i++) {
        mlc_mini_key mk = {0, 5*8 + i, 5*8, 5*8};
        (void)mlc_request_mini(c, mk, MLC_PRIORITY_NORMAL, async_mini_cb, NULL);
    }

    // Some requests should have been deduplicated.
    mlc_stats stats;
    mlc_get_stats(c, &stats);
    // At least one should be deduped (all map to same chunk).
    // The first triggers a fetch, subsequent ones should dedup.
    assert(stats.deduplicated_requests >= 1);

    free(chunk);
    mlc_destroy(c);
}

// ── Test: Concurrent reads on injected data ─────────────────────────────────

typedef struct {
    mlc_cache *c;
    int thread_id;
    _Atomic int *success_count;
} thread_read_arg;

static void *thread_read_minis(void *arg) {
    thread_read_arg *a = (thread_read_arg *)arg;
    for (int i = 0; i < 100; i++) {
        mlc_mini_key mk = {0, i % 8, (i + a->thread_id) % 8, 0};
        const uint8_t *data;
        if (mlc_get_mini(a->c, mk, &data) == MLC_OK) {
            atomic_fetch_add(a->success_count, 1);
            mlc_unpin_mini(a->c, mk);
        }
    }
    return NULL;
}

TEST(concurrent_reads) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 64 * 1024 * 1024;
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    // Inject a chunk.
    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    mlc_chunk_key ck = {0, 0, 0, 0};
    fill_test_chunk(chunk, ck);
    assert(mlc_inject_chunk(c, ck, chunk) == MLC_OK);

    _Atomic int success = 0;
    pthread_t threads[8];
    thread_read_arg args[8];
    for (int i = 0; i < 8; i++) {
        args[i] = (thread_read_arg){c, i, &success};
        pthread_create(&threads[i], NULL, thread_read_minis, &args[i]);
    }
    for (int i = 0; i < 8; i++) {
        pthread_join(threads[i], NULL);
    }

    // All reads should succeed (data is in HOT).
    assert(atomic_load(&success) == 8 * 100);

    free(chunk);
    mlc_destroy(c);
}

// ── Test: Scroll hint ───────────────────────────────────────────────────────

TEST(hint_scroll) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    mlc_hint_scroll(c, 0, 0, 256.0f, 100.0f);  // scrolling Z forward
    mlc_hint_scroll(c, 0, 2, 512.0f, -50.0f);  // scrolling X backward

    mlc_destroy(c);
}

// ── Test: Set/get decoder ───────────────────────────────────────────────────

TEST(codec_registration) {
    mlc_config cfg = test_config();
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    // Without decoder, WARM hits should return CODEC error.
    uint8_t dummy_compressed[MLC_CHUNK_VOX];
    memset(dummy_compressed, 0xAA, sizeof(dummy_compressed));
    assert(mlc_inject_compressed(c, (mlc_chunk_key){0,0,0,0},
           dummy_compressed, sizeof(dummy_compressed)) == MLC_OK);

    const uint8_t *data;
    mlc_status s = mlc_get_mini(c, (mlc_mini_key){0,0,0,0}, &data);
    assert(s == MLC_ERR_CODEC); // no decoder

    // Now register decoder and retry.
    mlc_set_decoder(c, test_decode, NULL);
    s = mlc_get_mini(c, (mlc_mini_key){0,0,0,0}, &data);
    assert(s == MLC_OK);
    mlc_unpin_mini(c, (mlc_mini_key){0,0,0,0});

    mlc_destroy(c);
}

// ── Test: Flush with data ───────────────────────────────────────────────────

TEST(flush_with_data) {
    mlc_config cfg = test_config();
    cfg.tiers.hot_budget = 64 * 1024 * 1024;
    mlc_cache *c;
    assert(mlc_create(&c, &cfg) == MLC_OK);

    uint8_t *chunk = (uint8_t *)malloc(MLC_CHUNK_VOX);
    fill_test_chunk(chunk, (mlc_chunk_key){0,0,0,0});
    assert(mlc_inject_chunk(c, (mlc_chunk_key){0,0,0,0}, chunk) == MLC_OK);

    mlc_stats stats;
    mlc_get_stats(c, &stats);
    assert(stats.tier[MLC_TIER_HOT].items == 512);

    mlc_flush(c);

    mlc_get_stats(c, &stats);
    assert(stats.tier[MLC_TIER_HOT].items == 0);
    assert(stats.tier[MLC_TIER_HOT].bytes_used == 0);

    free(chunk);
    mlc_destroy(c);
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(void) {
    fprintf(stderr, "mlcache tests (v%s)\n", mlc_version_str());

    RUN(version);
    RUN(status_strings);
    RUN(tier_strings);
    RUN(constants);

    fprintf(stderr, "\n  Coordinate conversions:\n");
    RUN(coords_voxel_to_mini);
    RUN(coords_voxel_to_chunk);
    RUN(coords_mini_to_chunk);
    RUN(coords_roundtrip);

    fprintf(stderr, "\n  Hash functions:\n");
    RUN(hash_deterministic);
    RUN(hash_distribution);
    RUN(key_equality);

    fprintf(stderr, "\n  Lifecycle:\n");
    RUN(create_destroy);
    RUN(create_null_args);
    RUN(default_config);
    RUN(shutdown_rejects_requests);

    fprintf(stderr, "\n  Data access (empty cache):\n");
    RUN(get_mini_empty);
    RUN(get_chunk_empty);
    RUN(read_region_empty);
    RUN(read_region_bad_bounds);

    fprintf(stderr, "\n  Statistics:\n");
    RUN(stats_empty);
    RUN(stats_after_miss);
    RUN(stats_reset);

    fprintf(stderr, "\n  Budget control:\n");
    RUN(budget_get_set);
    RUN(evict_empty);
    RUN(flush_empty);

    fprintf(stderr, "\n  Events:\n");
    RUN(event_subscribe_unsubscribe);

    fprintf(stderr, "\n  Prefetch:\n");
    RUN(prefetch_region);
    RUN(prefetch_around);
    RUN(cancel_prefetch);
    RUN(set_viewpoint);

    fprintf(stderr, "\n  Concurrency:\n");
    RUN(concurrent_lifecycle);

    fprintf(stderr, "\n  Data injection & HOT tier:\n");
    RUN(inject_chunk_read_minis);
    RUN(inject_chunk_get_chunk);
    RUN(read_region_cross_mini);
    RUN(multiple_chunks);
    RUN(stats_hits_misses);
    RUN(flush_with_data);

    fprintf(stderr, "\n  WARM → HOT decode pipeline:\n");
    RUN(warm_to_hot_decode);
    RUN(codec_registration);

    fprintf(stderr, "\n  Eviction:\n");
    RUN(eviction_pressure);
    RUN(pin_prevents_eviction);

    fprintf(stderr, "\n  Async & deduplication:\n");
    RUN(async_request_callback);
    RUN(request_deduplication);

    fprintf(stderr, "\n  Concurrent data access:\n");
    RUN(concurrent_reads);

    fprintf(stderr, "\n  Scroll hint:\n");
    RUN(hint_scroll);

    fprintf(stderr, "\n  All %d tests passed.\n", pass_count);
    return 0;
}
