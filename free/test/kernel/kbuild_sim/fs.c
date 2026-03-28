/*
 * fs.c - Simulated kernel filesystem layer
 *
 * Exercises:
 *   - Designated initializers (file_operations struct)
 *   - Function pointers
 *   - Static variables
 *   - Bitfields
 */
#include "types.h"

/* --- File operations structure (designated initializers) --- */

struct file;
struct inode;

typedef long loff_t;

struct file_operations {
    int (*open)(struct inode *, struct file *);
    ssize_t (*read)(struct file *, char *, size_t, loff_t *);
    ssize_t (*write)(struct file *, const char *, size_t, loff_t *);
    int (*release)(struct inode *, struct file *);
    long (*ioctl)(struct file *, unsigned int, unsigned long);
};

/* Inode with bitfields */
struct inode {
    u32 i_ino;
    u16 i_mode;
    struct {
        unsigned int i_type  : 4;
        unsigned int i_perm  : 12;
        unsigned int i_flags : 16;
    } i_bits;
    u32 i_size;
    u32 i_nlink;
};

struct file {
    struct inode *f_inode;
    const struct file_operations *f_op;
    loff_t f_pos;
    unsigned int f_flags;
};

/* --- Static data --- */

static int open_count = 0;

/* --- Function implementations --- */

static int my_open(struct inode *inode, struct file *filp)
{
    open_count++;
    filp->f_pos = 0;
    (void)inode;
    return 0;
}

static ssize_t my_read(struct file *filp, char *buf, size_t count,
                        loff_t *ppos)
{
    ssize_t remaining;
    (void)buf;
    remaining = (ssize_t)(filp->f_inode->i_size - (u32)*ppos);
    if (remaining <= 0)
        return 0;
    if ((ssize_t)count > remaining)
        count = (size_t)remaining;
    *ppos += (loff_t)count;
    return (ssize_t)count;
}

static ssize_t my_write(struct file *filp, const char *buf,
                         size_t count, loff_t *ppos)
{
    (void)filp;
    (void)buf;
    *ppos += (loff_t)count;
    return (ssize_t)count;
}

static int my_release(struct inode *inode, struct file *filp)
{
    open_count--;
    (void)inode;
    (void)filp;
    return 0;
}

static long my_ioctl(struct file *filp, unsigned int cmd,
                      unsigned long arg)
{
    (void)filp;
    (void)arg;
    switch (cmd) {
    case 0x01:
        return 0;
    case 0x02:
        return (long)open_count;
    default:
        return -1;
    }
}

/* Designated initializer - key kernel pattern */
static const struct file_operations my_fops = {
    .open    = my_open,
    .read    = my_read,
    .write   = my_write,
    .release = my_release,
    .ioctl   = my_ioctl
};

/* --- Public interface --- */

int fs_get_open_count(void)
{
    return open_count;
}

const struct file_operations *fs_get_ops(void)
{
    return &my_fops;
}

/* Test bitfield access */
int fs_check_inode(void)
{
    struct inode node;
    node.i_ino = 42;
    node.i_mode = 0755;
    node.i_bits.i_type = 8;
    node.i_bits.i_perm = 0755;
    node.i_bits.i_flags = 0;
    node.i_size = 4096;
    node.i_nlink = 1;

    if (node.i_bits.i_type != 8)
        return 1;
    if (node.i_bits.i_perm != 0755)
        return 2;
    if (node.i_ino != 42)
        return 3;
    return 0;
}

EXPORT_SYMBOL(fs_get_ops);
