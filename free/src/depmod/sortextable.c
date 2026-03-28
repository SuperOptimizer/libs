/*
 * sortextable.c - Sort kernel exception tables
 * Usage: free-sortextable vmlinux
 * Reads ELF, finds __ex_table section, sorts entries by address, writes back.
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
#define O_WRONLY    1
#define O_RDWR     2
#define O_CREAT    64
#define O_TRUNC    512
#define AT_FDCWD   -100
#define BUF_SIZE   (32 * 1024 * 1024)

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
    write_str(2, "free-sortextable: ");
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

/* Static buffers */
static char file_buf[BUF_SIZE];

/* Read entire file */
static long read_file(const char *path, char *buf, long bufsize)
{
    int fd;
    long total;
    long n;

    fd = (int)sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        write_str(2, "free-sortextable: cannot open ");
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

/* Write buffer to file */
static void write_file(const char *path, const char *buf, long size)
{
    int fd;
    long written;
    long n;

    fd = (int)sys_openat(AT_FDCWD, path,
                         O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        write_str(2, "free-sortextable: cannot create ");
        write_str(2, path);
        write_str(2, "\n");
        sys_exit(1);
    }
    written = 0;
    while (written < size) {
        n = sys_write(fd, buf + written, size - written);
        if (n <= 0) {
            die("write failed");
        }
        written += n;
    }
    sys_close(fd);
}

/*
 * Exception table entry on aarch64:
 * Each entry is 8 bytes: two i32 fields.
 *   insn  - offset to faulting instruction (relative to entry)
 *   fixup - offset to fixup code (relative to entry)
 * Absolute address = section_addr + entry_offset + insn.
 * We sort by the absolute address of the faulting instruction.
 */

struct extable_entry {
    i32 insn;
    i32 fixup;
};

/* Compute absolute address for an extable entry */
static u64 extable_addr(u64 section_addr, long entry_offset, i32 insn_rel)
{
    return (u64)((i64)section_addr + (i64)entry_offset + (i64)insn_rel);
}

/* Simple insertion sort for exception table entries */
static void sort_extable(char *data, u64 section_addr, long num_entries)
{
    struct extable_entry *entries;
    struct extable_entry tmp;
    long i;
    long j;
    u64 addr_i;
    u64 addr_j;

    entries = (struct extable_entry *)data;

    for (i = 1; i < num_entries; i++) {
        tmp = entries[i];
        addr_i = extable_addr(section_addr, i * 8, tmp.insn);
        j = i - 1;
        while (j >= 0) {
            addr_j = extable_addr(section_addr, j * 8, entries[j].insn);
            if (addr_j <= addr_i) {
                break;
            }
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = tmp;
    }
}

/* ---- entry point ---- */
int main(int argc, char **argv)
{
    const char *filename;
    long fsize;
    Elf64_Ehdr *ehdr;
    Elf64_Shdr *shdrs;
    const char *shstrtab;
    int i;
    int found;

    /* Handle --version early */
    if (argc >= 2 && streq(argv[1], "--version")) {
        write_str(1, "free-sortextable (free) 0.1.0\n");
        sys_exit(0);
    }

    if (argc < 2) {
        write_str(2, "Usage: free-sortextable file\n");
        sys_exit(1);
    }

    filename = argv[1];

    /* Read input ELF */
    fsize = read_file(filename, file_buf, BUF_SIZE);

    /* Parse ELF header */
    ehdr = (Elf64_Ehdr *)file_buf;
    if (fsize < (long)sizeof(Elf64_Ehdr)) {
        die("file too small");
    }
    if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
        die("not an ELF file");
    }
    if (ehdr->e_ident[4] != ELFCLASS64) {
        die("not 64-bit ELF");
    }

    shdrs = (Elf64_Shdr *)(file_buf + ehdr->e_shoff);
    shstrtab = file_buf + shdrs[ehdr->e_shstrndx].sh_offset;

    /* Find __ex_table section */
    found = 0;
    for (i = 1; i < ehdr->e_shnum; i++) {
        const char *name;
        name = shstrtab + shdrs[i].sh_name;
        if (streq(name, "__ex_table")) {
            long num_entries;
            num_entries = (long)(shdrs[i].sh_size / 8);
            if (num_entries > 0) {
                write_str(1, "Sorting ");
                {
                    char buf[24];
                    int pos;
                    long val;
                    pos = 23;
                    buf[pos] = '\0';
                    val = num_entries;
                    if (val == 0) {
                        buf[--pos] = '0';
                    } else {
                        while (val > 0) {
                            buf[--pos] = '0' + (char)(val % 10);
                            val /= 10;
                        }
                    }
                    write_str(1, &buf[pos]);
                }
                write_str(1, " exception table entries\n");

                sort_extable(file_buf + shdrs[i].sh_offset,
                             shdrs[i].sh_addr, num_entries);
            }
            found = 1;
            break;
        }
    }

    if (!found) {
        write_str(1, "No __ex_table section found\n");
        sys_exit(0);
    }

    /* Write back modified ELF */
    write_file(filename, file_buf, fsize);

    sys_exit(0);
    return 0;
}
