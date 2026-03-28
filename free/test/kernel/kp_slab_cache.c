/* Kernel pattern: slab cache allocator usage */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/kernel.h>

struct network_buffer {
    struct list_head link;
    unsigned int size;
    unsigned int offset;
    unsigned char data[0]; /* zero-length array for flexible member */
};

struct connection {
    int socket_fd;
    unsigned int state;
    unsigned long bytes_sent;
    unsigned long bytes_recv;
    struct list_head buffers;
    struct list_head link;
};

static struct kmem_cache *conn_cache;
static LIST_HEAD(active_connections);

static int init_conn_cache(void)
{
    conn_cache = KMEM_CACHE(connection, SLAB_HWCACHE_ALIGN);
    if (!conn_cache)
        return -1;
    return 0;
}

static struct connection *alloc_connection(int fd)
{
    struct connection *conn;

    conn = kmem_cache_alloc(conn_cache, GFP_KERNEL);
    if (!conn)
        return NULL;

    conn->socket_fd = fd;
    conn->state = 0;
    conn->bytes_sent = 0;
    conn->bytes_recv = 0;
    INIT_LIST_HEAD(&conn->buffers);
    list_add(&conn->link, &active_connections);

    return conn;
}

static void free_connection(struct connection *conn)
{
    list_del(&conn->link);
    kmem_cache_free(conn_cache, conn);
}

static int count_connections(void)
{
    struct connection *conn;
    int count = 0;
    list_for_each_entry(conn, &active_connections, link)
        count++;
    return count;
}

static void cleanup_conn_cache(void)
{
    if (conn_cache)
        kmem_cache_destroy(conn_cache);
}

void test_slab_cache(void)
{
    struct connection *c1;
    struct connection *c2;
    int cnt;

    if (init_conn_cache())
        return;

    c1 = alloc_connection(10);
    c2 = alloc_connection(20);

    cnt = count_connections();
    (void)cnt;

    if (c2)
        free_connection(c2);
    if (c1)
        free_connection(c1);

    cleanup_conn_cache();
}
