#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

/* File type bits */
#define S_IFMT   0170000  /* type of file mask */
#define S_IFSOCK 0140000  /* socket */
#define S_IFLNK  0120000  /* symbolic link */
#define S_IFREG  0100000  /* regular */
#define S_IFBLK  0060000  /* block special */
#define S_IFDIR  0040000  /* directory */
#define S_IFCHR  0020000  /* character special */
#define S_IFIFO  0010000  /* fifo */

/* File type test macros */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* Permission bits */
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRGRP  0040
#define S_IWGRP  0020
#define S_IXGRP  0010
#define S_IROTH  0004
#define S_IWOTH  0002
#define S_IXOTH  0001

/*
 * struct stat for aarch64 Linux (matches kernel struct stat).
 * Field sizes: dev_t=8, ino_t=8, mode_t=4, nlink_t=4,
 *              uid_t=4, gid_t=4, off_t=8.
 */
struct stat {
    dev_t     st_dev;
    ino_t     st_ino;
    mode_t    st_mode;
    nlink_t   st_nlink;
    uid_t     st_uid;
    gid_t     st_gid;
    dev_t     st_rdev;
    unsigned long __pad1;
    off_t     st_size;
    int       st_blksize;
    int       __pad2;
    long      st_blocks;
    long      st_atime_sec;
    long      st_atime_nsec;
    long      st_mtime_sec;
    long      st_mtime_nsec;
    long      st_ctime_sec;
    long      st_ctime_nsec;
    unsigned int __unused4;
    unsigned int __unused5;
};

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *path, struct stat *buf);

#endif
