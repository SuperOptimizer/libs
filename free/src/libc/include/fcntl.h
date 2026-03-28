#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

/* File access modes */
#define O_RDONLY   00
#define O_WRONLY   01
#define O_RDWR     02

/* File creation flags */
#define O_CREAT    0100
#define O_EXCL     0200
#define O_NOCTTY   0400
#define O_TRUNC    01000
#define O_APPEND   02000
#define O_NONBLOCK 04000
#define O_CLOEXEC  02000000

/* Special directory fd for *at() calls */
#define AT_FDCWD   (-100)

/* fcntl commands */
#define F_DUPFD    0
#define F_GETFD    1
#define F_SETFD    2
#define F_GETFL    3
#define F_SETFL    4
#define F_GETLK    5
#define F_SETLK    6
#define F_SETLKW   7

/* fcntl file descriptor flags */
#define FD_CLOEXEC 1

/* flock l_type values */
#define F_RDLCK    0
#define F_WRLCK    1
#define F_UNLCK    2

#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED
typedef unsigned int mode_t;
#endif

#ifndef _PID_T_DEFINED
#define _PID_T_DEFINED
typedef int pid_t;
#endif

/* File lock structure */
struct flock {
    short l_type;
    short l_whence;
    long  l_start;
    long  l_len;
    int   l_pid;
};

int open(const char *path, int flags, ...);
int openat(int dirfd, const char *path, int flags, ...);
int fcntl(int fd, int cmd, ...);

#endif /* _FCNTL_H */
