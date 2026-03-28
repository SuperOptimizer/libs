/* SPDX-License-Identifier: GPL-2.0 */
/* Stub fs.h for free-cc kernel compilation testing */
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/types.h>

#define PATH_MAX    4096

struct file;
struct inode;
struct super_block;
struct dentry;

static inline char *file_path(struct file *file, char *buf, int buflen)
{
    (void)file;
    (void)buf;
    (void)buflen;
    return buf;
}

#endif /* _LINUX_FS_H */
