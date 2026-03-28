#ifndef _ERRNO_H
#define _ERRNO_H

/* Provide both forms:
 * - __errno_location() for glibc-compatible linking
 * - errno macro for C code usage
 * When using freestanding free-libc, __errno_location is provided
 * by the libc itself.  When linking with glibc, it comes from glibc. */
extern int *__errno_location(void);
#define errno (*__errno_location())

#define EPERM   1
#define ENOENT  2
#define ESRCH   3
#define EINTR   4
#define EIO     5
#define ENOMEM  12
#define EACCES  13
#define EFAULT  14
#define EEXIST  17
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define EMFILE  24
#define ENOSPC  28
#define ERANGE  34
#define ENAMETOOLONG 36

#endif
