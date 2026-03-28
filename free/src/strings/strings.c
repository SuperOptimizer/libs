/*
 * strings.c - Extract printable strings from binary files
 * Usage: free-strings [-n N] [-t o|x] file
 * Pure C89, freestanding with OS syscalls
 */

#include "../../include/free.h"

/* ---- syscall wrappers (via __syscall from syscall.S) ---- */

extern long __syscall(long, long, long, long, long, long, long);

#define SYS_OPENAT     56
#define SYS_CLOSE      57
#define SYS_READ       63
#define SYS_WRITE      64
#define SYS_EXIT_GROUP 94

static long sys_openat(int dirfd, const char *path, int flags, int mode)
{
    return __syscall(SYS_OPENAT, (long)dirfd, (long)path,
                     (long)flags, (long)mode, 0, 0);
}

static long sys_read(int fd, void *buf, long count)
{
    return __syscall(SYS_READ, (long)fd, (long)buf, count, 0, 0, 0);
}

static long sys_write(int fd, const void *buf, long count)
{
    return __syscall(SYS_WRITE, (long)fd, (long)buf, count, 0, 0, 0);
}

static long sys_close(int fd)
{
    return __syscall(SYS_CLOSE, (long)fd, 0, 0, 0, 0, 0);
}

static void sys_exit(int code)
{
    __syscall(SYS_EXIT_GROUP, (long)code, 0, 0, 0, 0, 0);
    for (;;) {}
}

/* ---- constants ---- */
#define O_RDONLY    0
#define AT_FDCWD   -100
#define READ_BUF   (64 * 1024)
#define STR_BUF    4096

/* ---- utility functions ---- */

static void write_str(int fd, const char *s)
{
    long n;
    const char *p;

    n = 0;
    p = s;
    while (*p) {
        p++;
        n++;
    }
    sys_write(fd, s, n);
}

static void die(const char *msg)
{
    write_str(2, "free-strings: ");
    write_str(2, msg);
    write_str(2, "\n");
    sys_exit(1);
}

static int streq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int is_printable(unsigned char c)
{
    return c >= 0x20 && c <= 0x7E;
}

static char hex_char(int v)
{
    if (v < 10) {
        return '0' + (char)v;
    }
    return 'a' + (char)(v - 10);
}

/* Print offset in hex */
static void print_hex_offset(u64 off)
{
    char buf[20];
    int i;
    int started;

    buf[0] = ' ';
    buf[1] = ' ';
    for (i = 15; i >= 2; i--) {
        buf[i] = hex_char((int)(off & 0xf));
        off >>= 4;
    }
    buf[16] = ' ';
    buf[17] = '\0';

    /* skip leading zeros but keep at least one digit */
    started = 0;
    for (i = 2; i < 16; i++) {
        if (buf[i] != '0') {
            started = 1;
        }
        if (started) {
            break;
        }
    }
    if (!started) {
        i = 15;
    }
    sys_write(1, buf + i, 17 - i);
}

/* Print offset in octal */
static void print_oct_offset(u64 off)
{
    char buf[28];
    int i;
    int started;

    buf[0] = ' ';
    buf[1] = ' ';
    for (i = 23; i >= 2; i--) {
        buf[i] = '0' + (char)(off & 7);
        off >>= 3;
    }
    buf[24] = ' ';
    buf[25] = '\0';

    started = 0;
    for (i = 2; i < 24; i++) {
        if (buf[i] != '0') {
            started = 1;
        }
        if (started) {
            break;
        }
    }
    if (!started) {
        i = 23;
    }
    sys_write(1, buf + i, 25 - i);
}

/* ---- main ---- */

static unsigned char read_buf[READ_BUF];
static char str_buf[STR_BUF];

void _start(void);

int main(int argc, char **argv)
{
    int min_len;
    int show_offset;   /* 0=none, 'o'=octal, 'x'=hex */
    const char *filename;
    int fd;
    long nread;
    u64 file_offset;
    u64 str_start;
    int str_len;
    long i;
    int ai;

    min_len = 4;
    show_offset = 0;
    filename = NULL;

    /* Handle --version early */
    for (ai = 1; ai < argc; ai++) {
        if (streq(argv[ai], "--version")) {
            write_str(1, "GNU strings (free-strings) 2.42\n");
            sys_exit(0);
        }
    }

    /* parse arguments */
    ai = 1;
    while (ai < argc) {
        if (streq(argv[ai], "-n") && ai + 1 < argc) {
            /* parse number */
            const char *p;
            int n;

            ai++;
            p = argv[ai];
            n = 0;
            while (*p >= '0' && *p <= '9') {
                n = n * 10 + (*p - '0');
                p++;
            }
            if (n > 0) {
                min_len = n;
            }
        } else if (streq(argv[ai], "-t") && ai + 1 < argc) {
            ai++;
            if (argv[ai][0] == 'o') {
                show_offset = 'o';
            } else if (argv[ai][0] == 'x') {
                show_offset = 'x';
            } else {
                die("invalid -t argument: use 'o' or 'x'");
            }
        } else if (argv[ai][0] == '-') {
            die("unknown option");
        } else {
            filename = argv[ai];
        }
        ai++;
    }

    if (filename == NULL) {
        write_str(2, "Usage: free-strings [-n N] [-t o|x] file\n");
        sys_exit(1);
    }

    fd = (int)sys_openat(AT_FDCWD, filename, O_RDONLY, 0);
    if (fd < 0) {
        write_str(2, "free-strings: cannot open ");
        write_str(2, filename);
        write_str(2, "\n");
        sys_exit(1);
    }

    file_offset = 0;
    str_len = 0;
    str_start = 0;

    for (;;) {
        nread = sys_read(fd, read_buf, READ_BUF);
        if (nread <= 0) {
            break;
        }

        for (i = 0; i < nread; i++) {
            if (is_printable(read_buf[i])) {
                if (str_len == 0) {
                    str_start = file_offset + (u64)i;
                }
                if (str_len < STR_BUF - 1) {
                    str_buf[str_len] = (char)read_buf[i];
                }
                str_len++;
            } else {
                if (str_len >= min_len) {
                    int out_len;

                    out_len = str_len;
                    if (out_len > STR_BUF - 1) {
                        out_len = STR_BUF - 1;
                    }
                    str_buf[out_len] = '\0';

                    if (show_offset == 'x') {
                        print_hex_offset(str_start);
                    } else if (show_offset == 'o') {
                        print_oct_offset(str_start);
                    }
                    sys_write(1, str_buf, out_len);
                    sys_write(1, "\n", 1);
                }
                str_len = 0;
            }
        }

        file_offset += (u64)nread;
    }

    /* flush any trailing string */
    if (str_len >= min_len) {
        int out_len;

        out_len = str_len;
        if (out_len > STR_BUF - 1) {
            out_len = STR_BUF - 1;
        }
        str_buf[out_len] = '\0';

        if (show_offset == 'x') {
            print_hex_offset(str_start);
        } else if (show_offset == 'o') {
            print_oct_offset(str_start);
        }
        sys_write(1, str_buf, out_len);
        sys_write(1, "\n", 1);
    }

    sys_close(fd);
    sys_exit(0);
    return 0;
}
