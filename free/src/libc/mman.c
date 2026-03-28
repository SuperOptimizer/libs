/*
 * mman.c - Memory mapping functions for the free libc.
 * Pure C89. No external dependencies.
 */

#include <sys/mman.h>
#include <errno.h>

/* syscall interface (from syscall.S) */
long __syscall(long num, long a1, long a2, long a3,
               long a4, long a5, long a6);

#define SYS_MMAP     222
#define SYS_MUNMAP   215
#define SYS_MPROTECT 226

/* ------------------------------------------------------------------ */
/* mmap                                                                */
/* ------------------------------------------------------------------ */

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, long offset)
{
    long ret;

    ret = __syscall(SYS_MMAP, (long)addr, (long)length,
                    (long)prot, (long)flags, (long)fd, offset);
    if (ret < 0 && ret > -4096) {
        errno = (int)(-ret);
        return MAP_FAILED;
    }
    return (void *)ret;
}

/* ------------------------------------------------------------------ */
/* munmap                                                              */
/* ------------------------------------------------------------------ */

int munmap(void *addr, size_t length)
{
    long ret;

    ret = __syscall(SYS_MUNMAP, (long)addr, (long)length,
                    0, 0, 0, 0);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* mprotect                                                            */
/* ------------------------------------------------------------------ */

int mprotect(void *addr, size_t length, int prot)
{
    long ret;

    ret = __syscall(SYS_MPROTECT, (long)addr, (long)length,
                    (long)prot, 0, 0, 0);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    return 0;
}
