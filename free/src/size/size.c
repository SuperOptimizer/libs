/*
 * size.c - Section size reporter for the free toolchain
 * Usage: free-size [-A] file [file ...]
 * Pure C89, freestanding with OS syscalls
 */

#include "../../include/free.h"
#include "../../include/elf.h"

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
#define BUF_SIZE   (16 * 1024 * 1024)

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
    write_str(2, "free-size: ");
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

static int str_len(const char *s)
{
    int n;

    n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static char hex_char(int v)
{
    if (v < 10) {
        return '0' + (char)v;
    }
    return 'a' + (char)(v - 10);
}

static void print_hex(u64 val)
{
    char buf[20];
    int i;
    int started;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 17; i >= 2; i--) {
        buf[i] = hex_char((int)(val & 0xf));
        val >>= 4;
    }
    buf[18] = '\0';

    started = 0;
    for (i = 2; i < 17; i++) {
        if (buf[i] != '0') {
            started = 1;
        }
        if (started) {
            break;
        }
    }
    if (!started) {
        i = 17;
    }
    /* print "0x" prefix plus digits */
    sys_write(1, "0x", 2);
    sys_write(1, buf + i, 18 - i);
}

/* Right-justify decimal in field of given width */
static void print_dec_width(u64 val, int width)
{
    char buf[24];
    int pos;
    int len;
    int i;

    pos = 23;
    buf[pos] = '\0';
    if (val == 0) {
        buf[--pos] = '0';
    } else {
        while (val > 0) {
            buf[--pos] = '0' + (char)(val % 10);
            val /= 10;
        }
    }
    len = 23 - pos;
    for (i = len; i < width; i++) {
        write_str(1, " ");
    }
    write_str(1, &buf[pos]);
}

static void print_padded(const char *s, int width)
{
    int len;
    int i;

    len = str_len(s);
    write_str(1, s);
    for (i = len; i < width; i++) {
        write_str(1, " ");
    }
}

/* ---- file buffer ---- */
static char file_buf[BUF_SIZE];

static long read_file(const char *path, char *buf, long bufsize)
{
    int fd;
    long total;
    long n;

    fd = (int)sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        write_str(2, "free-size: cannot open ");
        write_str(2, path);
        write_str(2, "\n");
        sys_exit(1);
    }
    total = 0;
    while (total < bufsize) {
        n = sys_read(fd, buf + total, bufsize - total);
        if (n <= 0) {
            break;
        }
        total += n;
    }
    sys_close(fd);
    return total;
}

/* ---- size reporting ---- */

static void size_default(const char *path)
{
    long filesz;
    const Elf64_Ehdr *ehdr;
    const Elf64_Shdr *shdrs;
    const char *shstrtab;
    u64 text_size;
    u64 data_size;
    u64 bss_size;
    u64 total;
    int i;

    filesz = read_file(path, file_buf, BUF_SIZE);
    if (filesz < (long)sizeof(Elf64_Ehdr)) {
        die("file too small");
    }

    ehdr = (const Elf64_Ehdr *)file_buf;
    if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
        die("not an ELF file");
    }

    shdrs = (const Elf64_Shdr *)(file_buf + ehdr->e_shoff);
    shstrtab = file_buf + shdrs[ehdr->e_shstrndx].sh_offset;

    text_size = 0;
    data_size = 0;
    bss_size = 0;

    for (i = 0; i < (int)ehdr->e_shnum; i++) {
        const char *name;
        u64 sz;

        name = shstrtab + shdrs[i].sh_name;
        sz = shdrs[i].sh_size;

        if (shdrs[i].sh_type == SHT_NOBITS) {
            /* .bss and similar */
            bss_size += sz;
        } else if (shdrs[i].sh_flags & SHF_ALLOC) {
            if (shdrs[i].sh_flags & SHF_EXECINSTR) {
                text_size += sz;
            } else if (shdrs[i].sh_flags & SHF_WRITE) {
                data_size += sz;
            } else {
                /* read-only data counts as text */
                text_size += sz;
            }
        }
        (void)name;
    }

    total = text_size + data_size + bss_size;

    /* header */
    write_str(1, "   text\t   data\t    bss\t    dec\t    hex\tfilename\n");

    /* values */
    print_dec_width(text_size, 7);
    write_str(1, "\t");
    print_dec_width(data_size, 7);
    write_str(1, "\t");
    print_dec_width(bss_size, 7);
    write_str(1, "\t");
    print_dec_width(total, 7);
    write_str(1, "\t");
    print_hex(total);
    write_str(1, "\t");
    write_str(1, path);
    write_str(1, "\n");
}

static void size_sysv(const char *path)
{
    long filesz;
    const Elf64_Ehdr *ehdr;
    const Elf64_Shdr *shdrs;
    const char *shstrtab;
    u64 total;
    int i;

    filesz = read_file(path, file_buf, BUF_SIZE);
    if (filesz < (long)sizeof(Elf64_Ehdr)) {
        die("file too small");
    }

    ehdr = (const Elf64_Ehdr *)file_buf;
    if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
        die("not an ELF file");
    }

    shdrs = (const Elf64_Shdr *)(file_buf + ehdr->e_shoff);
    shstrtab = file_buf + shdrs[ehdr->e_shstrndx].sh_offset;

    write_str(1, path);
    write_str(1, "  :\n");
    write_str(1, "section             size      addr\n");

    total = 0;
    for (i = 0; i < (int)ehdr->e_shnum; i++) {
        const char *name;

        if (shdrs[i].sh_type == SHT_NULL) {
            continue;
        }
        if (!(shdrs[i].sh_flags & SHF_ALLOC) &&
            shdrs[i].sh_type != SHT_NOBITS) {
            /* skip non-loadable sections unless they're bss-like */
            if (shdrs[i].sh_type != SHT_PROGBITS &&
                shdrs[i].sh_type != SHT_NOBITS) {
                continue;
            }
        }

        name = shstrtab + shdrs[i].sh_name;
        print_padded(name, 20);
        print_dec_width(shdrs[i].sh_size, 10);
        write_str(1, "  ");
        print_dec_width(shdrs[i].sh_addr, 10);
        write_str(1, "\n");

        if (shdrs[i].sh_flags & SHF_ALLOC || shdrs[i].sh_type == SHT_NOBITS) {
            total += shdrs[i].sh_size;
        }
    }

    write_str(1, "Total               ");
    print_dec_width(total, 10);
    write_str(1, "\n");
}

/* ---- main ---- */

void _start(void);

int main(int argc, char **argv)
{
    int sysv_format;
    int ai;
    int file_count;

    sysv_format = 0;
    file_count = 0;

    /* Handle --version early */
    for (ai = 1; ai < argc; ai++) {
        if (streq(argv[ai], "--version")) {
            write_str(1, "GNU size (free-size) 2.42\n");
            sys_exit(0);
        }
    }

    /* parse arguments */
    ai = 1;
    while (ai < argc) {
        if (streq(argv[ai], "-A")) {
            sysv_format = 1;
        } else if (streq(argv[ai], "-h") || streq(argv[ai], "--help")) {
            write_str(1, "Usage: free-size [-A] file [file ...]\n");
            sys_exit(0);
        } else if (argv[ai][0] == '-') {
            write_str(2, "free-size: unknown option: ");
            write_str(2, argv[ai]);
            write_str(2, "\n");
            sys_exit(1);
        } else {
            break;
        }
        ai++;
    }

    if (ai >= argc) {
        write_str(2, "Usage: free-size [-A] file [file ...]\n");
        sys_exit(1);
    }

    while (ai < argc) {
        if (sysv_format) {
            size_sysv(argv[ai]);
        } else {
            size_default(argv[ai]);
        }
        ai++;
        file_count++;
    }

    sys_exit(0);
    return 0;
}
