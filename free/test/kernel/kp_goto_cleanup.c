/* Kernel pattern: goto-based error cleanup */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel.h>

struct pipeline_stage {
    void *buffer;
    size_t buf_size;
    int (*process)(struct pipeline_stage *, const void *, size_t);
};

struct pipeline {
    struct pipeline_stage *stages;
    int num_stages;
    unsigned long flags;
    void *context;
    char name[32];
};

static int pipeline_create(struct pipeline **pp, int num_stages,
                           const char *name)
{
    struct pipeline *p;
    int i;

    if (!pp || num_stages <= 0)
        return -EINVAL;

    p = kzalloc(sizeof(*p), GFP_KERNEL);
    if (!p)
        return -ENOMEM;

    p->stages = kcalloc(num_stages, sizeof(struct pipeline_stage), GFP_KERNEL);
    if (!p->stages)
        goto err_free_pipeline;

    for (i = 0; i < num_stages; i++) {
        p->stages[i].buf_size = 4096;
        p->stages[i].buffer = kmalloc(p->stages[i].buf_size, GFP_KERNEL);
        if (!p->stages[i].buffer)
            goto err_free_buffers;
    }

    p->context = kmalloc(256, GFP_KERNEL);
    if (!p->context)
        goto err_free_buffers;

    p->num_stages = num_stages;
    p->flags = 0;

    /* Copy name safely */
    {
        int j;
        for (j = 0; name[j] && j < 31; j++)
            p->name[j] = name[j];
        p->name[j] = '\0';
    }

    *pp = p;
    return 0;

err_free_buffers:
    for (i = i - 1; i >= 0; i--)
        kfree(p->stages[i].buffer);
    kfree(p->stages);
err_free_pipeline:
    kfree(p);
    return -ENOMEM;
}

static void pipeline_destroy(struct pipeline *p)
{
    int i;

    if (!p)
        return;

    kfree(p->context);
    for (i = 0; i < p->num_stages; i++)
        kfree(p->stages[i].buffer);
    kfree(p->stages);
    kfree(p);
}

void test_goto_cleanup(void)
{
    struct pipeline *p = NULL;
    int ret;

    ret = pipeline_create(&p, 5, "test_pipe");
    if (ret == 0)
        pipeline_destroy(p);
}
