/* Kernel pattern: simple memory pool allocator */
#include <linux/types.h>
#include <linux/kernel.h>

#define POOL_BLOCK_SIZE 256
#define POOL_MAX_BLOCKS 64

struct mem_block {
    unsigned char data[POOL_BLOCK_SIZE];
    int in_use;
};

struct mem_pool {
    struct mem_block blocks[POOL_MAX_BLOCKS];
    int free_count;
    int total_allocs;
    int total_frees;
};

static void pool_init(struct mem_pool *pool)
{
    int i;
    for (i = 0; i < POOL_MAX_BLOCKS; i++) {
        pool->blocks[i].in_use = 0;
        memset(pool->blocks[i].data, 0, POOL_BLOCK_SIZE);
    }
    pool->free_count = POOL_MAX_BLOCKS;
    pool->total_allocs = 0;
    pool->total_frees = 0;
}

static void *pool_alloc(struct mem_pool *pool)
{
    int i;
    if (pool->free_count == 0)
        return NULL;

    for (i = 0; i < POOL_MAX_BLOCKS; i++) {
        if (!pool->blocks[i].in_use) {
            pool->blocks[i].in_use = 1;
            pool->free_count--;
            pool->total_allocs++;
            return pool->blocks[i].data;
        }
    }
    return NULL;
}

static void pool_free(struct mem_pool *pool, void *ptr)
{
    int i;
    if (!ptr)
        return;

    for (i = 0; i < POOL_MAX_BLOCKS; i++) {
        if (pool->blocks[i].data == ptr) {
            pool->blocks[i].in_use = 0;
            pool->free_count++;
            pool->total_frees++;
            return;
        }
    }
}

static int pool_usage_percent(const struct mem_pool *pool)
{
    int used = POOL_MAX_BLOCKS - pool->free_count;
    return (used * 100) / POOL_MAX_BLOCKS;
}

void test_mempool(void)
{
    struct mem_pool pool;
    void *ptrs[10];
    int i;
    int usage;

    pool_init(&pool);

    for (i = 0; i < 10; i++)
        ptrs[i] = pool_alloc(&pool);

    usage = pool_usage_percent(&pool);
    (void)usage;

    for (i = 0; i < 5; i++)
        pool_free(&pool, ptrs[i]);

    usage = pool_usage_percent(&pool);
    (void)usage;
}
