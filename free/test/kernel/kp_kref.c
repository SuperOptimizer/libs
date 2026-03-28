/* Kernel pattern: reference counting with kref */
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/kernel.h>

struct my_device {
    struct kref refcount;
    char name[64];
    int id;
    unsigned long flags;
    void (*cleanup)(struct my_device *);
};

static void my_device_release(struct kref *kref)
{
    struct my_device *dev = container_of(kref, struct my_device, refcount);
    if (dev->cleanup)
        dev->cleanup(dev);
    kfree(dev);
}

static struct my_device *my_device_create(const char *name, int id)
{
    struct my_device *dev;
    int i;

    dev = kmalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return NULL;

    kref_init(&dev->refcount);
    for (i = 0; name[i] && i < 63; i++)
        dev->name[i] = name[i];
    dev->name[i] = '\0';
    dev->id = id;
    dev->flags = 0;
    dev->cleanup = NULL;

    return dev;
}

static struct my_device *my_device_get(struct my_device *dev)
{
    if (dev)
        kref_get(&dev->refcount);
    return dev;
}

static void my_device_put(struct my_device *dev)
{
    if (dev)
        kref_put(&dev->refcount, my_device_release);
}

void test_kref(void)
{
    struct my_device *dev;
    struct my_device *ref;

    dev = my_device_create("test_dev", 42);
    if (!dev)
        return;

    ref = my_device_get(dev);
    my_device_put(ref);
    my_device_put(dev);
}
