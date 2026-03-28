/*
 * dirent.c - Directory entry functions for the free libc.
 * Pure C89. No external dependencies.
 */

#include <dirent.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* syscall interface (from syscall.S) */
long __syscall(long num, long a1, long a2, long a3,
               long a4, long a5, long a6);

#define SYS_OPENAT    56
#define SYS_CLOSE     57
#define SYS_GETDENTS64 61

#define AT_FDCWD      (-100)
#define O_RDONLY      0
#define O_DIRECTORY   0x4000

#define DIR_BUFSIZ    1024

/*
 * Internal DIR structure.
 * Holds the fd and a buffer for getdents64 results.
 */
struct _DIR {
    int    fd;
    char   buf[DIR_BUFSIZ];
    int    buf_pos;
    int    buf_len;
    struct dirent entry;
};

/* ------------------------------------------------------------------ */
/* kernel linux_dirent64 layout (for parsing getdents64 output)        */
/* ------------------------------------------------------------------ */

struct linux_dirent64 {
    unsigned long  d_ino;
    long           d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[1]; /* variable length */
};

/* ------------------------------------------------------------------ */
/* opendir                                                             */
/* ------------------------------------------------------------------ */

DIR *opendir(const char *name)
{
    long fd;
    DIR *dir;

    fd = __syscall(SYS_OPENAT, (long)AT_FDCWD, (long)name,
                   (long)(O_RDONLY | O_DIRECTORY), 0, 0, 0);
    if (fd < 0) {
        errno = (int)(-fd);
        return NULL;
    }

    dir = (DIR *)malloc(sizeof(DIR));
    if (dir == NULL) {
        __syscall(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
        return NULL;
    }
    memset(dir, 0, sizeof(DIR));
    dir->fd = (int)fd;
    return dir;
}

/* ------------------------------------------------------------------ */
/* readdir                                                             */
/* ------------------------------------------------------------------ */

struct dirent *readdir(DIR *dirp)
{
    struct linux_dirent64 *lde;
    long ret;
    size_t name_len;

    if (dirp == NULL) {
        return NULL;
    }

    /* refill buffer if exhausted */
    if (dirp->buf_pos >= dirp->buf_len) {
        ret = __syscall(SYS_GETDENTS64, (long)dirp->fd,
                        (long)dirp->buf, (long)DIR_BUFSIZ,
                        0, 0, 0);
        if (ret <= 0) {
            if (ret < 0) {
                errno = (int)(-ret);
            }
            return NULL;
        }
        dirp->buf_len = (int)ret;
        dirp->buf_pos = 0;
    }

    /* parse current entry */
    lde = (struct linux_dirent64 *)(dirp->buf + dirp->buf_pos);
    dirp->buf_pos += lde->d_reclen;

    /* copy to our dirent struct */
    dirp->entry.d_ino = lde->d_ino;
    dirp->entry.d_off = lde->d_off;
    dirp->entry.d_reclen = lde->d_reclen;
    dirp->entry.d_type = lde->d_type;

    name_len = (size_t)(lde->d_reclen) -
               (size_t)((char *)lde->d_name - (char *)lde);
    if (name_len >= sizeof(dirp->entry.d_name)) {
        name_len = sizeof(dirp->entry.d_name) - 1;
    }
    memcpy(dirp->entry.d_name, lde->d_name, name_len);
    dirp->entry.d_name[name_len] = '\0';

    return &dirp->entry;
}

/* ------------------------------------------------------------------ */
/* closedir                                                            */
/* ------------------------------------------------------------------ */

int closedir(DIR *dirp)
{
    int ret;

    if (dirp == NULL) {
        return -1;
    }
    ret = (int)__syscall(SYS_CLOSE, (long)dirp->fd, 0, 0, 0, 0, 0);
    free(dirp);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    return 0;
}
