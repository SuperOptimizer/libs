/* Kernel pattern: errno codes and error propagation */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>

struct config {
    int speed;
    int duplex;
    int autoneg;
    unsigned long flags;
};

static int validate_speed(int speed)
{
    switch (speed) {
    case 10:
    case 100:
    case 1000:
    case 10000:
        return 0;
    default:
        return -EINVAL;
    }
}

static int validate_duplex(int duplex)
{
    if (duplex != 0 && duplex != 1)
        return -EINVAL;
    return 0;
}

static int apply_config(struct config *cfg)
{
    int ret;

    if (!cfg)
        return -EFAULT;

    ret = validate_speed(cfg->speed);
    if (ret)
        return ret;

    ret = validate_duplex(cfg->duplex);
    if (ret)
        return ret;

    if (cfg->flags & 0x80000000UL)
        return -EBUSY;

    return 0;
}

static const char *err_to_string(int err)
{
    switch (err) {
    case 0:        return "success";
    case -EINVAL:  return "invalid argument";
    case -ENOMEM:  return "out of memory";
    case -EBUSY:   return "device busy";
    case -EFAULT:  return "bad address";
    case -ENOENT:  return "not found";
    case -EPERM:   return "operation not permitted";
    case -ENOSPC:  return "no space left";
    case -EIO:     return "I/O error";
    case -EAGAIN:  return "try again";
    default:       return "unknown error";
    }
}

void test_errno(void)
{
    struct config good = { .speed = 1000, .duplex = 1, .autoneg = 1, .flags = 0 };
    struct config bad  = { .speed = 42, .duplex = 1, .autoneg = 0, .flags = 0 };
    int ret;
    const char *msg;

    ret = apply_config(&good);
    msg = err_to_string(ret);
    (void)msg;

    ret = apply_config(&bad);
    msg = err_to_string(ret);
    (void)msg;

    ret = apply_config(NULL);
    msg = err_to_string(ret);
    (void)msg;
}
