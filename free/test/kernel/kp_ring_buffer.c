/* Kernel pattern: ring buffer implementation */
#include <linux/types.h>
#include <linux/kernel.h>

#define RING_SIZE 1024
#define RING_MASK (RING_SIZE - 1)

struct ring_buffer {
    unsigned char data[RING_SIZE];
    unsigned int head;
    unsigned int tail;
    unsigned int count;
};

static void ring_init(struct ring_buffer *rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    memset(rb->data, 0, RING_SIZE);
}

static int ring_full(const struct ring_buffer *rb)
{
    return rb->count == RING_SIZE;
}

static int ring_empty(const struct ring_buffer *rb)
{
    return rb->count == 0;
}

static int ring_put(struct ring_buffer *rb, unsigned char val)
{
    if (ring_full(rb))
        return -1;
    rb->data[rb->head & RING_MASK] = val;
    rb->head++;
    rb->count++;
    return 0;
}

static int ring_get(struct ring_buffer *rb, unsigned char *val)
{
    if (ring_empty(rb))
        return -1;
    *val = rb->data[rb->tail & RING_MASK];
    rb->tail++;
    rb->count--;
    return 0;
}

static unsigned int ring_used(const struct ring_buffer *rb)
{
    return rb->count;
}

static unsigned int ring_free(const struct ring_buffer *rb)
{
    return RING_SIZE - rb->count;
}

static int ring_write(struct ring_buffer *rb, const unsigned char *buf,
                      unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++) {
        if (ring_put(rb, buf[i]) < 0)
            return (int)i;
    }
    return (int)len;
}

static int ring_read(struct ring_buffer *rb, unsigned char *buf,
                     unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++) {
        if (ring_get(rb, &buf[i]) < 0)
            return (int)i;
    }
    return (int)len;
}

void test_ring_buffer(void)
{
    struct ring_buffer rb;
    unsigned char buf[32];
    unsigned int used;
    unsigned int free_space;
    int written;
    int read_count;
    int i;

    ring_init(&rb);

    for (i = 0; i < 32; i++)
        buf[i] = (unsigned char)(i + 'A');

    written = ring_write(&rb, buf, 32);
    used = ring_used(&rb);
    free_space = ring_free(&rb);

    memset(buf, 0, 32);
    read_count = ring_read(&rb, buf, 16);

    (void)written; (void)used; (void)free_space; (void)read_count;
}
