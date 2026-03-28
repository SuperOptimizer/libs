/* Kernel pattern: container_of and type embedding */
#include <linux/types.h>
#include <linux/list.h>
#include <linux/kernel.h>

struct base_object {
    int type;
    const char *name;
    unsigned long flags;
};

struct char_device {
    struct base_object base;
    int major;
    int minor;
    unsigned long buffer_size;
};

struct block_device {
    struct base_object base;
    unsigned long sector_size;
    unsigned long num_sectors;
};

struct net_device {
    struct base_object base;
    unsigned char mac[6];
    unsigned int mtu;
    unsigned long rx_bytes;
    unsigned long tx_bytes;
};

static struct char_device *to_char_device(struct base_object *obj)
{
    return container_of(obj, struct char_device, base);
}

static struct block_device *to_block_device(struct base_object *obj)
{
    return container_of(obj, struct block_device, base);
}

static struct net_device *to_net_device(struct base_object *obj)
{
    return container_of(obj, struct net_device, base);
}

static unsigned long get_device_size(struct base_object *obj)
{
    switch (obj->type) {
    case 1: {
        struct char_device *cdev = to_char_device(obj);
        return cdev->buffer_size;
    }
    case 2: {
        struct block_device *bdev = to_block_device(obj);
        return bdev->sector_size * bdev->num_sectors;
    }
    case 3: {
        struct net_device *ndev = to_net_device(obj);
        return ndev->mtu;
    }
    default:
        return 0;
    }
}

void test_container_of(void)
{
    struct char_device cdev;
    struct block_device bdev;
    struct net_device ndev;
    unsigned long s1, s2, s3;

    cdev.base.type = 1;
    cdev.base.name = "tty0";
    cdev.buffer_size = 4096;

    bdev.base.type = 2;
    bdev.base.name = "sda";
    bdev.sector_size = 512;
    bdev.num_sectors = 1000000;

    ndev.base.type = 3;
    ndev.base.name = "eth0";
    ndev.mtu = 1500;

    s1 = get_device_size(&cdev.base);
    s2 = get_device_size(&bdev.base);
    s3 = get_device_size(&ndev.base);

    (void)s1; (void)s2; (void)s3;
}
