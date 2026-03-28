#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>

/* Standard file descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* access() mode flags */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

int access(const char *path, int mode);

/* POSIX types for aarch64 Linux */
typedef long          ssize_t;
typedef long          off_t;
typedef int           pid_t;

/* Basic I/O */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int     close(int fd);
off_t   lseek(int fd, off_t offset, int whence);

/* Process control */
pid_t   getpid(void);
pid_t   getppid(void);
pid_t   fork(void);
int     execve(const char *path, char *const argv[], char *const envp[]);

/* Filesystem */
int     unlink(const char *path);
int     rmdir(const char *path);
char   *getcwd(char *buf, size_t size);
int     chdir(const char *path);

/* Memory */
void   *sbrk(long increment);
int     brk(void *addr);

/* Timers */
unsigned int alarm(unsigned int seconds);

#endif
