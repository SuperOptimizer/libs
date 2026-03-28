/* Kernel pattern: workqueue usage */
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/kernel.h>

struct async_task {
    struct work_struct work;
    int task_id;
    unsigned long data;
    void (*callback)(struct async_task *);
};

static void async_work_handler(struct work_struct *work)
{
    struct async_task *task = container_of(work, struct async_task, work);
    if (task->callback)
        task->callback(task);
}

static struct async_task *create_async_task(int id, unsigned long data,
                                            void (*cb)(struct async_task *))
{
    struct async_task *task;

    task = kmalloc(sizeof(*task), GFP_KERNEL);
    if (!task)
        return NULL;

    INIT_WORK(&task->work, async_work_handler);
    task->task_id = id;
    task->data = data;
    task->callback = cb;

    return task;
}

static void submit_task(struct async_task *task)
{
    schedule_work(&task->work);
}

static void my_callback(struct async_task *task)
{
    task->data *= 2;
}

void test_workqueue(void)
{
    struct async_task *t1;
    struct async_task *t2;

    t1 = create_async_task(1, 100, my_callback);
    t2 = create_async_task(2, 200, my_callback);

    if (t1)
        submit_task(t1);
    if (t2)
        submit_task(t2);
}
