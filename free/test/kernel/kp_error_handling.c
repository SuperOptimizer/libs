/* Kernel pattern: error handling with ERR_PTR/IS_ERR */
#include <linux/types.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/kernel.h>

struct resource_obj {
    int id;
    unsigned long size;
    void *data;
    int refcount;
};

static struct resource_obj *alloc_resource(int id, unsigned long size)
{
    struct resource_obj *obj;

    if (size == 0)
        return ERR_PTR(-EINVAL);

    if (size > 1024 * 1024)
        return ERR_PTR(-ENOMEM);

    obj = kmalloc(sizeof(*obj), GFP_KERNEL);
    if (!obj)
        return ERR_PTR(-ENOMEM);

    obj->data = kmalloc(size, GFP_KERNEL);
    if (!obj->data) {
        kfree(obj);
        return ERR_PTR(-ENOMEM);
    }

    obj->id = id;
    obj->size = size;
    obj->refcount = 1;
    return obj;
}

static void free_resource(struct resource_obj *obj)
{
    if (!obj || IS_ERR(obj))
        return;
    obj->refcount--;
    if (obj->refcount <= 0) {
        kfree(obj->data);
        kfree(obj);
    }
}

static int init_subsystem(void)
{
    struct resource_obj *r1;
    struct resource_obj *r2;
    struct resource_obj *r3;
    int err;

    r1 = alloc_resource(1, 256);
    if (IS_ERR(r1))
        return PTR_ERR(r1);

    r2 = alloc_resource(2, 512);
    if (IS_ERR(r2)) {
        err = PTR_ERR(r2);
        goto err_free_r1;
    }

    r3 = alloc_resource(3, 0);
    if (IS_ERR(r3)) {
        err = PTR_ERR(r3);
        goto err_free_r2;
    }

    free_resource(r3);
    free_resource(r2);
    free_resource(r1);
    return 0;

err_free_r2:
    free_resource(r2);
err_free_r1:
    free_resource(r1);
    return err;
}

void test_error_handling(void)
{
    int ret = init_subsystem();
    (void)ret;
}
