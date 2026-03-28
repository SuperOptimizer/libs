/* Kernel pattern: refcount-protected objects */
#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/kernel.h>

struct shared_resource {
    refcount_t refs;
    unsigned long id;
    void *data;
    size_t data_size;
    void (*destructor)(struct shared_resource *);
};

static void resource_init(struct shared_resource *r, unsigned long id,
                          void *data, size_t size)
{
    refcount_set(&r->refs, 1);
    r->id = id;
    r->data = data;
    r->data_size = size;
    r->destructor = NULL;
}

static struct shared_resource *resource_get(struct shared_resource *r)
{
    if (r && refcount_inc_not_zero(&r->refs))
        return r;
    return NULL;
}

static void resource_put(struct shared_resource *r)
{
    if (r && refcount_dec_and_test(&r->refs)) {
        if (r->destructor)
            r->destructor(r);
    }
}

static int resource_refcount(const struct shared_resource *r)
{
    return refcount_read(&r->refs);
}

void test_refcount(void)
{
    struct shared_resource res;
    struct shared_resource *ref;
    int cnt;
    char data[64];

    resource_init(&res, 100, data, sizeof(data));

    ref = resource_get(&res);
    cnt = resource_refcount(&res);
    (void)cnt;

    if (ref)
        resource_put(ref);
    resource_put(&res);
}
