/*
 * stdio.c - Standard I/O functions for the free libc.
 * Pure C89. No external dependencies.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* syscall interface (from syscall.S) */
long __syscall(long num, long a1, long a2, long a3,
               long a4, long a5, long a6);

/* syscall numbers */
#define SYS_OPENAT     56
#define SYS_CLOSE      57
#define SYS_LSEEK      62
#define SYS_READ       63
#define SYS_WRITE      64
#define SYS_UNLINKAT   35
#define SYS_RENAMEAT   38

/* openat constants */
#define AT_FDCWD       (-100)
#define O_RDONLY       0
#define O_WRONLY       1
#define O_RDWR         2
#define O_CREAT        0x40
#define O_TRUNC        0x200
#define O_APPEND       0x400

/* FILE flag bits */
#define F_READ   1
#define F_WRITE  2
#define F_APPEND 4
#define F_MYBUF  8   /* buffer was allocated by us (for fopen'd files) */

/* ------------------------------------------------------------------ */
/* Static FILE structs for stdin, stdout, stderr                      */
/* ------------------------------------------------------------------ */

static FILE _stdin_file  = { STDIN_FILENO,  F_READ,  0, 0, {0}, 0, 0 };
static FILE _stdout_file = { STDOUT_FILENO, F_WRITE, 0, 0, {0}, 0, 0 };
static FILE _stderr_file = { STDERR_FILENO, F_WRITE, 0, 0, {0}, 0, 0 };

FILE *stdin  = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static int flush_write_buf(FILE *f)
{
    long ret;
    size_t written;
    size_t remaining;

    if (f->buf_pos == 0) {
        return 0;
    }

    written = 0;
    remaining = f->buf_pos;
    while (remaining > 0) {
        ret = __syscall(SYS_WRITE, (long)f->fd,
                        (long)(f->buf + written),
                        (long)remaining, 0, 0, 0);
        if (ret < 0) {
            f->error = 1;
            return EOF;
        }
        written += (size_t)ret;
        remaining -= (size_t)ret;
    }
    f->buf_pos = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* File open/close                                                    */
/* ------------------------------------------------------------------ */

FILE *fopen(const char *path, const char *mode)
{
    int oflags = 0;
    int fflags = 0;
    long fd;
    FILE *f;

    if (mode[0] == 'r') {
        if (mode[1] == '+') {
            oflags = O_RDWR;
            fflags = F_READ | F_WRITE;
        } else {
            oflags = O_RDONLY;
            fflags = F_READ;
        }
    } else if (mode[0] == 'w') {
        if (mode[1] == '+') {
            oflags = O_RDWR | O_CREAT | O_TRUNC;
            fflags = F_READ | F_WRITE;
        } else {
            oflags = O_WRONLY | O_CREAT | O_TRUNC;
            fflags = F_WRITE;
        }
    } else if (mode[0] == 'a') {
        if (mode[1] == '+') {
            oflags = O_RDWR | O_CREAT | O_APPEND;
            fflags = F_READ | F_WRITE | F_APPEND;
        } else {
            oflags = O_WRONLY | O_CREAT | O_APPEND;
            fflags = F_WRITE | F_APPEND;
        }
    } else {
        return NULL;
    }

    /* 0666 = owner/group/other rw */
    fd = __syscall(SYS_OPENAT, (long)AT_FDCWD, (long)path,
                   (long)oflags, 0666L, 0, 0);
    if (fd < 0) {
        errno = (int)(-fd);
        return NULL;
    }

    f = (FILE *)malloc(sizeof(FILE));
    if (f == NULL) {
        __syscall(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
        return NULL;
    }
    memset(f, 0, sizeof(FILE));
    f->fd = (int)fd;
    f->flags = fflags;
    return f;
}

int fclose(FILE *f)
{
    int ret;

    if (f == NULL) {
        return EOF;
    }

    ret = 0;
    if (f->flags & F_WRITE) {
        if (flush_write_buf(f) != 0) {
            ret = EOF;
        }
    }

    if (__syscall(SYS_CLOSE, (long)f->fd, 0, 0, 0, 0, 0) < 0) {
        ret = EOF;
    }

    /* only free if not one of the static stdio files */
    if (f != stdin && f != stdout && f != stderr) {
        free(f);
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/* Read / Write                                                       */
/* ------------------------------------------------------------------ */

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
    size_t total;
    size_t done;
    size_t avail;
    size_t chunk;
    char *dst;
    long ret;

    if (size == 0 || nmemb == 0) {
        return 0;
    }

    total = size * nmemb;
    done = 0;
    dst = (char *)ptr;

    /* first, drain any data in the read buffer */
    if (f->buf_len > 0 && f->buf_pos < f->buf_len) {
        avail = f->buf_len - f->buf_pos;
        chunk = avail < total ? avail : total;
        memcpy(dst, f->buf + f->buf_pos, chunk);
        f->buf_pos += chunk;
        done += chunk;
    }

    /* read remaining directly from fd */
    while (done < total) {
        ret = __syscall(SYS_READ, (long)f->fd,
                        (long)(dst + done),
                        (long)(total - done), 0, 0, 0);
        if (ret < 0) {
            f->error = 1;
            break;
        }
        if (ret == 0) {
            f->eof = 1;
            break;
        }
        done += (size_t)ret;
    }

    return done / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)
{
    size_t total;
    size_t done;
    size_t space;
    size_t chunk;
    const char *src;

    if (size == 0 || nmemb == 0) {
        return 0;
    }

    total = size * nmemb;
    done = 0;
    src = (const char *)ptr;

    while (done < total) {
        space = BUFSIZ - f->buf_pos;
        chunk = (total - done) < space ? (total - done) : space;
        memcpy(f->buf + f->buf_pos, src + done, chunk);
        f->buf_pos += chunk;
        done += chunk;

        if (f->buf_pos >= BUFSIZ) {
            if (flush_write_buf(f) != 0) {
                break;
            }
        }
    }

    return done / size;
}

/* ------------------------------------------------------------------ */
/* Seek / Tell / Flush / Status                                       */
/* ------------------------------------------------------------------ */

int fseek(FILE *f, long offset, int whence)
{
    long ret;

    /* flush any pending writes */
    if (f->flags & F_WRITE) {
        flush_write_buf(f);
    }

    /* discard any buffered read data */
    f->buf_pos = 0;
    f->buf_len = 0;
    f->eof = 0;

    ret = __syscall(SYS_LSEEK, (long)f->fd, offset, (long)whence,
                    0, 0, 0);
    if (ret < 0) {
        f->error = 1;
        return -1;
    }
    return 0;
}

long ftell(FILE *f)
{
    long pos;

    pos = __syscall(SYS_LSEEK, (long)f->fd, 0L, (long)SEEK_CUR,
                    0, 0, 0);
    if (pos < 0) {
        f->error = 1;
        return -1L;
    }

    /* adjust for buffered but unread data */
    if (f->buf_len > f->buf_pos) {
        pos -= (long)(f->buf_len - f->buf_pos);
    }

    /* adjust for buffered but unwritten data */
    if (f->flags & F_WRITE) {
        pos += (long)f->buf_pos;
    }

    return pos;
}

int fflush(FILE *f)
{
    if (f == NULL) {
        /* flush all writable streams (just stdout/stderr for now) */
        fflush(stdout);
        fflush(stderr);
        return 0;
    }
    if (f->flags & F_WRITE) {
        return flush_write_buf(f);
    }
    return 0;
}

int feof(FILE *f)
{
    return f->eof;
}

int ferror(FILE *f)
{
    return f->error;
}

/* ------------------------------------------------------------------ */
/* Character I/O                                                      */
/* ------------------------------------------------------------------ */

int fgetc(FILE *f)
{
    long ret;

    if (f->eof) {
        return EOF;
    }

    /* if read buffer is empty, refill it */
    if (f->buf_pos >= f->buf_len) {
        ret = __syscall(SYS_READ, (long)f->fd, (long)f->buf,
                        (long)BUFSIZ, 0, 0, 0);
        if (ret < 0) {
            f->error = 1;
            return EOF;
        }
        if (ret == 0) {
            f->eof = 1;
            return EOF;
        }
        f->buf_pos = 0;
        f->buf_len = (size_t)ret;
    }

    return (unsigned char)f->buf[f->buf_pos++];
}

int fputc(int c, FILE *f)
{
    unsigned char ch = (unsigned char)c;

    f->buf[f->buf_pos++] = (char)ch;

    /* flush if buffer is full, or on newline for stdout */
    if (f->buf_pos >= BUFSIZ ||
        (ch == '\n' && f->fd == STDOUT_FILENO)) {
        if (flush_write_buf(f) != 0) {
            return EOF;
        }
    }

    return ch;
}

char *fgets(char *s, int size, FILE *f)
{
    int i;
    int c;

    if (size <= 0) {
        return NULL;
    }

    i = 0;
    while (i < size - 1) {
        c = fgetc(f);
        if (c == EOF) {
            if (i == 0) {
                return NULL;
            }
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *f)
{
    size_t len = strlen(s);

    if (fwrite(s, 1, len, f) != len) {
        return EOF;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* printf family: core formatter                                      */
/* ------------------------------------------------------------------ */

/*
 * Output callback: either writes to a FILE or to a string buffer.
 * sn_buf/sn_pos/sn_size are used for string output.
 * fp is used for file output.
 */
struct fmt_state {
    FILE  *fp;
    char  *sn_buf;
    size_t sn_pos;
    size_t sn_size;
    int    count;
};

static void fmt_putc(struct fmt_state *st, char c)
{
    if (st->fp != NULL) {
        fputc(c, st->fp);
    } else if (st->sn_buf != NULL) {
        if (st->sn_pos + 1 < st->sn_size) {
            st->sn_buf[st->sn_pos] = c;
        }
        st->sn_pos++;
    }
    st->count++;
}

static void fmt_puts(struct fmt_state *st, const char *s, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        fmt_putc(st, s[i]);
    }
}

static void fmt_pad(struct fmt_state *st, char padch, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        fmt_putc(st, padch);
    }
}

/*
 * Render an unsigned long to a decimal/hex/octal string.
 * Returns pointer into the provided buffer (filled from the end).
 */
static char *render_unsigned(unsigned long val, int base, int uppercase,
                             char *buf, int bufsize, int *outlen)
{
    char *p;
    const char *digits_lower = "0123456789abcdef";
    const char *digits_upper = "0123456789ABCDEF";
    const char *digits;

    digits = uppercase ? digits_upper : digits_lower;
    p = buf + bufsize;
    *p = '\0';

    if (val == 0) {
        *(--p) = '0';
    } else {
        while (val != 0) {
            *(--p) = digits[val % (unsigned long)base];
            val /= (unsigned long)base;
        }
    }
    *outlen = (int)((buf + bufsize) - p);
    return p;
}

static int do_format(struct fmt_state *st, const char *fmt, va_list ap)
{
    char numbuf[24]; /* enough for 64-bit in decimal */
    char *numstr;
    int numlen;
    int width;
    int left_align;
    int zero_pad;
    int is_long;
    int pad;
    char padch;
    unsigned long uval;
    long sval;
    const char *sptr;
    int ch;

    while (*fmt != '\0') {
        if (*fmt != '%') {
            fmt_putc(st, *fmt);
            fmt++;
            continue;
        }
        fmt++; /* skip '%' */

        /* flags */
        left_align = 0;
        zero_pad = 0;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') {
                left_align = 1;
            }
            if (*fmt == '0') {
                zero_pad = 1;
            }
            fmt++;
        }
        if (left_align) {
            zero_pad = 0;
        }

        /* width */
        width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left_align = 1;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* skip precision for now (consume .N) */
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                (void)va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    fmt++;
                }
            }
        }

        /* length modifier */
        is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                fmt++; /* treat ll same as l on 64-bit */
            }
        } else if (*fmt == 'z') {
            is_long = 1;
            fmt++;
        }

        padch = zero_pad ? '0' : ' ';

        /* conversion specifier */
        switch (*fmt) {
        case 'd':
        case 'i':
            if (is_long) {
                sval = va_arg(ap, long);
            } else {
                sval = (long)va_arg(ap, int);
            }
            if (sval < 0) {
                uval = (unsigned long)(-(sval + 1)) + 1UL;
            } else {
                uval = (unsigned long)sval;
            }
            numstr = render_unsigned(uval, 10, 0, numbuf,
                                     (int)sizeof(numbuf) - 1, &numlen);
            if (sval < 0) {
                numlen++; /* account for '-' in width calc */
            }
            pad = width - numlen;
            if (!left_align && !zero_pad) {
                fmt_pad(st, ' ', pad);
            }
            if (sval < 0) {
                fmt_putc(st, '-');
            }
            if (!left_align && zero_pad) {
                fmt_pad(st, '0', pad);
            }
            fmt_puts(st, numstr, (size_t)(sval < 0 ? numlen - 1 : numlen));
            if (left_align) {
                fmt_pad(st, ' ', pad);
            }
            break;

        case 'u':
            if (is_long) {
                uval = va_arg(ap, unsigned long);
            } else {
                uval = (unsigned long)va_arg(ap, unsigned int);
            }
            numstr = render_unsigned(uval, 10, 0, numbuf,
                                     (int)sizeof(numbuf) - 1, &numlen);
            pad = width - numlen;
            if (!left_align) {
                fmt_pad(st, padch, pad);
            }
            fmt_puts(st, numstr, (size_t)numlen);
            if (left_align) {
                fmt_pad(st, ' ', pad);
            }
            break;

        case 'x':
        case 'X':
            if (is_long) {
                uval = va_arg(ap, unsigned long);
            } else {
                uval = (unsigned long)va_arg(ap, unsigned int);
            }
            numstr = render_unsigned(uval, 16, (*fmt == 'X'), numbuf,
                                     (int)sizeof(numbuf) - 1, &numlen);
            pad = width - numlen;
            if (!left_align) {
                fmt_pad(st, padch, pad);
            }
            fmt_puts(st, numstr, (size_t)numlen);
            if (left_align) {
                fmt_pad(st, ' ', pad);
            }
            break;

        case 'o':
            if (is_long) {
                uval = va_arg(ap, unsigned long);
            } else {
                uval = (unsigned long)va_arg(ap, unsigned int);
            }
            numstr = render_unsigned(uval, 8, 0, numbuf,
                                     (int)sizeof(numbuf) - 1, &numlen);
            pad = width - numlen;
            if (!left_align) {
                fmt_pad(st, padch, pad);
            }
            fmt_puts(st, numstr, (size_t)numlen);
            if (left_align) {
                fmt_pad(st, ' ', pad);
            }
            break;

        case 'p':
            uval = (unsigned long)(unsigned long)va_arg(ap, void *);
            numstr = render_unsigned(uval, 16, 0, numbuf,
                                     (int)sizeof(numbuf) - 1, &numlen);
            /* print "0x" prefix */
            fmt_putc(st, '0');
            fmt_putc(st, 'x');
            pad = width - numlen - 2;
            if (!left_align) {
                fmt_pad(st, '0', pad);
            }
            fmt_puts(st, numstr, (size_t)numlen);
            if (left_align) {
                fmt_pad(st, ' ', pad);
            }
            break;

        case 's':
            sptr = va_arg(ap, const char *);
            if (sptr == NULL) {
                sptr = "(null)";
            }
            numlen = (int)strlen(sptr);
            pad = width - numlen;
            if (!left_align) {
                fmt_pad(st, ' ', pad);
            }
            fmt_puts(st, sptr, (size_t)numlen);
            if (left_align) {
                fmt_pad(st, ' ', pad);
            }
            break;

        case 'c':
            ch = va_arg(ap, int);
            pad = width - 1;
            if (!left_align) {
                fmt_pad(st, ' ', pad);
            }
            fmt_putc(st, (char)ch);
            if (left_align) {
                fmt_pad(st, ' ', pad);
            }
            break;

        case '%':
            fmt_putc(st, '%');
            break;

        case '\0':
            /* format string ended with a lone '%' */
            return st->count;

        default:
            /* unknown specifier: emit as-is */
            fmt_putc(st, '%');
            fmt_putc(st, *fmt);
            break;
        }
        fmt++;
    }

    return st->count;
}

/* ------------------------------------------------------------------ */
/* Public printf-family functions                                     */
/* ------------------------------------------------------------------ */

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    struct fmt_state st;
    int ret;

    st.fp = f;
    st.sn_buf = NULL;
    st.sn_pos = 0;
    st.sn_size = 0;
    st.count = 0;

    ret = do_format(&st, fmt, ap);
    return ret;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
    struct fmt_state st;
    int ret;

    st.fp = NULL;
    st.sn_buf = str;
    st.sn_pos = 0;
    st.sn_size = size;
    st.count = 0;

    ret = do_format(&st, fmt, ap);

    /* null-terminate */
    if (str != NULL && size > 0) {
        if (st.sn_pos < size) {
            str[st.sn_pos] = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }

    return ret;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return ret;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vfprintf(f, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(str, (size_t)-1, fmt, ap);
    va_end(ap);
    return ret;
}

int snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return ret;
}

/* ------------------------------------------------------------------ */
/* tmpfile / rewind                                                   */
/* ------------------------------------------------------------------ */

FILE *tmpfile(void)
{
    static int tmpfile_counter = 0;
    char path[64];
    FILE *f;
    int n;

    n = tmpfile_counter++;
    sprintf(path, "/tmp/.free-tmp-%d", n);

    f = fopen(path, "w+");
    if (f != NULL) {
        /* unlink immediately so the file disappears on close */
        __syscall(SYS_UNLINKAT, (long)AT_FDCWD, (long)path, 0, 0, 0, 0);
    }
    return f;
}

void rewind(FILE *f)
{
    fseek(f, 0L, SEEK_SET);
    f->eof = 0;
    f->error = 0;
}

/* ------------------------------------------------------------------ */
/* Simple convenience I/O                                             */
/* ------------------------------------------------------------------ */

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) {
        return EOF;
    }
    if (fputc('\n', stdout) == EOF) {
        return EOF;
    }
    return 0;
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int getchar(void)
{
    return fgetc(stdin);
}

/* ------------------------------------------------------------------ */
/* perror                                                             */
/* ------------------------------------------------------------------ */

static const char *errno_msg(int err)
{
    switch (err) {
    case 0:          return "Success";
    case EPERM:      return "Operation not permitted";
    case ENOENT:     return "No such file or directory";
    case ESRCH:      return "No such process";
    case EINTR:      return "Interrupted system call";
    case EIO:        return "I/O error";
    case ENOMEM:     return "Out of memory";
    case EACCES:     return "Permission denied";
    case EFAULT:     return "Bad address";
    case EEXIST:     return "File exists";
    case ENOTDIR:    return "Not a directory";
    case EISDIR:     return "Is a directory";
    case EINVAL:     return "Invalid argument";
    case EMFILE:     return "Too many open files";
    case ENOSPC:     return "No space left on device";
    case ERANGE:     return "Result too large";
    case ENAMETOOLONG: return "File name too long";
    default:         return "Unknown error";
    }
}

void perror(const char *s)
{
    if (s != NULL && *s != '\0') {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(errno_msg(errno), stderr);
    fputc('\n', stderr);
}

/* ------------------------------------------------------------------ */
/* remove / rename                                                    */
/* ------------------------------------------------------------------ */

int remove(const char *path)
{
    long ret;

    /* try unlinkat first (works for files) */
    ret = __syscall(SYS_UNLINKAT, (long)AT_FDCWD, (long)path, 0,
                    0, 0, 0);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    return 0;
}

int rename(const char *oldpath, const char *newpath)
{
    long ret;

    ret = __syscall(SYS_RENAMEAT, (long)AT_FDCWD, (long)oldpath,
                    (long)AT_FDCWD, (long)newpath, 0, 0);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    return 0;
}
