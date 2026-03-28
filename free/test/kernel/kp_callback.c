/* Kernel pattern: function pointers and callbacks */
#include <linux/types.h>
#include <linux/kernel.h>

/* ops-style dispatch table */
struct file_operations {
    int (*open)(const char *path, int flags);
    int (*read)(int fd, void *buf, size_t count);
    int (*write)(int fd, const void *buf, size_t count);
    int (*close)(int fd);
    long (*ioctl)(int fd, unsigned int cmd, unsigned long arg);
    int (*mmap)(int fd, void *addr, size_t length);
};

struct driver {
    const char *name;
    int version;
    const struct file_operations *fops;
    int (*probe)(struct driver *drv);
    void (*remove)(struct driver *drv);
};

/* Concrete implementations */
static int my_open(const char *path, int flags)
{
    (void)path;
    (void)flags;
    return 3; /* fake fd */
}

static int my_read(int fd, void *buf, size_t count)
{
    (void)fd;
    memset(buf, 0, count);
    return (int)count;
}

static int my_write(int fd, const void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    return (int)count;
}

static int my_close(int fd)
{
    (void)fd;
    return 0;
}

static const struct file_operations my_fops = {
    .open  = my_open,
    .read  = my_read,
    .write = my_write,
    .close = my_close,
    .ioctl = NULL,
    .mmap  = NULL,
};

static int my_probe(struct driver *drv)
{
    (void)drv;
    return 0;
}

static void my_remove(struct driver *drv)
{
    (void)drv;
}

static struct driver my_driver = {
    .name    = "my_driver",
    .version = 1,
    .fops    = &my_fops,
    .probe   = my_probe,
    .remove  = my_remove,
};

void test_callbacks(void)
{
    int fd;
    char buf[64];
    int n;

    if (my_driver.probe)
        my_driver.probe(&my_driver);

    if (my_driver.fops && my_driver.fops->open) {
        fd = my_driver.fops->open("/dev/test", 0);
        if (fd >= 0) {
            n = my_driver.fops->read(fd, buf, sizeof(buf));
            (void)n;
            my_driver.fops->close(fd);
        }
    }

    if (my_driver.remove)
        my_driver.remove(&my_driver);
}
