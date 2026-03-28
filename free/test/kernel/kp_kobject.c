/* Kernel pattern: kobject lifecycle */
#include <linux/types.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/kernel.h>

struct my_subsys {
    struct kobject kobj;
    int instance_id;
    unsigned long config;
};

static void my_subsys_release(struct kobject *kobj)
{
    struct my_subsys *sub = container_of(kobj, struct my_subsys, kobj);
    kfree(sub);
}

static struct kobj_type my_subsys_ktype = {
    .release = my_subsys_release,
};

static struct my_subsys *create_subsys(int id, struct kobject *parent)
{
    struct my_subsys *sub;
    int ret;

    sub = kzalloc(sizeof(*sub), GFP_KERNEL);
    if (!sub)
        return NULL;

    sub->instance_id = id;
    sub->config = 0xDEAD;

    ret = kobject_init_and_add(&sub->kobj, &my_subsys_ktype,
                               parent, "subsys%d", id);
    if (ret) {
        kobject_put(&sub->kobj);
        return NULL;
    }

    return sub;
}

static void destroy_subsys(struct my_subsys *sub)
{
    kobject_del(&sub->kobj);
    kobject_put(&sub->kobj);
}

void test_kobject_lifecycle(void)
{
    struct my_subsys *s1;
    struct my_subsys *s2;

    s1 = create_subsys(1, NULL);
    if (!s1)
        return;

    s2 = create_subsys(2, &s1->kobj);

    if (s2)
        destroy_subsys(s2);
    destroy_subsys(s1);
}
