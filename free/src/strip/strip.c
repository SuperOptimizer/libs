/*
 * strip.c - Symbol stripping tool for the free toolchain
 * Usage: free-strip [--strip-debug] [--strip-all] [-o output] file
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
#define O_CREAT    64
#define O_TRUNC    512
#define AT_FDCWD   -100

#define BUF_SIZE       (16 * 1024 * 1024)
#define OUT_BUF_SIZE   (16 * 1024 * 1024)
#define MAX_SECTIONS   256

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
    write_str(2, "free-strip: ");
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

static int str_startswith(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s != *prefix) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}

static void mem_copy(void *dst, const void *src, long n)
{
    char *d;
    const char *s;
    long i;

    d = (char *)dst;
    s = (const char *)src;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static void mem_set(void *dst, int c, long n)
{
    char *d;
    long i;

    d = (char *)dst;
    for (i = 0; i < n; i++) {
        d[i] = (char)c;
    }
}

/* Static buffers */
static char file_buf[BUF_SIZE];
static char out_buf[OUT_BUF_SIZE];

/* Read entire file */
static long read_file(const char *path, char *buf, long bufsize)
{
    int fd;
    long total;
    long n;

    fd = (int)sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        write_str(2, "free-strip: cannot open ");
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
        write_str(2, "free-strip: cannot create ");
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

/* ---- ELF parsing ---- */

static const Elf64_Ehdr *elf_ehdr;
static const Elf64_Shdr *elf_shdrs;
static const Elf64_Phdr *elf_phdrs;
static const char *elf_shstrtab;
static const char *elf_data;
static long elf_size;

static void parse_elf(void)
{
    elf_ehdr = (const Elf64_Ehdr *)elf_data;
    if (elf_size < (long)sizeof(Elf64_Ehdr)) {
        die("file too small");
    }
    if (elf_ehdr->e_ident[0] != ELFMAG0 || elf_ehdr->e_ident[1] != ELFMAG1 ||
        elf_ehdr->e_ident[2] != ELFMAG2 || elf_ehdr->e_ident[3] != ELFMAG3) {
        die("not an ELF file");
    }
    if (elf_ehdr->e_ident[4] != ELFCLASS64) {
        die("not 64-bit ELF");
    }
    elf_shdrs = (const Elf64_Shdr *)(elf_data + elf_ehdr->e_shoff);
    if (elf_ehdr->e_shstrndx < elf_ehdr->e_shnum) {
        elf_shstrtab = elf_data + elf_shdrs[elf_ehdr->e_shstrndx].sh_offset;
    } else {
        elf_shstrtab = "";
    }
    if (elf_ehdr->e_phoff != 0 && elf_ehdr->e_phnum > 0) {
        elf_phdrs = (const Elf64_Phdr *)(elf_data + elf_ehdr->e_phoff);
    } else {
        elf_phdrs = NULL;
    }
}

static const char *section_name(int idx)
{
    if (idx == 0 || idx >= elf_ehdr->e_shnum) {
        return "";
    }
    return elf_shstrtab + elf_shdrs[idx].sh_name;
}

/* ---- strip modes ---- */
#define STRIP_ALL   0
#define STRIP_DEBUG 1

/*
 * Determine if a section should be removed.
 * mode = STRIP_ALL:
 *   Remove .symtab, .strtab, all .debug_* sections, and .rela.* sections
 *   that reference removed sections.
 *   Keep .dynsym, .dynstr (needed for shared libs).
 * mode = STRIP_DEBUG:
 *   Remove only .debug_* sections, keep symbol table.
 */
static int should_strip(int idx, int mode)
{
    const char *sname;
    const Elf64_Shdr *sh;

    if (idx == 0) {
        return 0; /* never remove null section */
    }

    sname = section_name(idx);
    sh = &elf_shdrs[idx];

    /* Always remove .debug_* sections */
    if (str_startswith(sname, ".debug_")) {
        return 1;
    }

    /* Also remove .comment section in strip-all mode */
    if (mode == STRIP_ALL && streq(sname, ".comment")) {
        return 1;
    }

    if (mode == STRIP_ALL) {
        /* Remove .symtab and its associated .strtab */
        if (sh->sh_type == SHT_SYMTAB) {
            return 1;
        }

        /* Remove .strtab (but not .dynstr or .shstrtab) */
        if (sh->sh_type == SHT_STRTAB) {
            if (streq(sname, ".strtab")) {
                return 1;
            }
        }

        /* Remove .rela sections that reference the symtab */
        if (sh->sh_type == SHT_RELA) {
            /* Check if the linked section (symtab) is being removed */
            if (sh->sh_link > 0 &&
                sh->sh_link < (u32)elf_ehdr->e_shnum) {
                if (elf_shdrs[sh->sh_link].sh_type == SHT_SYMTAB) {
                    return 1;
                }
            }
        }
    }

    return 0;
}

/* Align a value up */
static u64 align_up(u64 val, u64 align)
{
    if (align == 0) {
        return val;
    }
    return (val + align - 1) & ~(align - 1);
}

/* Perform the strip operation */
static void do_strip(const char *output_path, int mode)
{
    Elf64_Ehdr new_ehdr;
    Elf64_Shdr new_shdrs[MAX_SECTIONS];
    int old_to_new[MAX_SECTIONS];
    int new_shnum;
    int new_shstrndx;
    long out_pos;
    long shdr_pos;
    int i;
    int j;

    /* Phase 1: decide which sections to keep */
    new_shnum = 0;

    /* Section 0 always kept */
    old_to_new[0] = 0;
    new_shnum = 1;

    for (i = 1; i < elf_ehdr->e_shnum; i++) {
        if (should_strip(i, mode)) {
            old_to_new[i] = -1;
        } else {
            old_to_new[i] = new_shnum;
            new_shnum++;
        }
    }

    if (new_shnum > MAX_SECTIONS) {
        die("too many sections");
    }

    /* Phase 2: write output */
    out_pos = (long)sizeof(Elf64_Ehdr);

    /* Copy program headers if present */
    if (elf_phdrs != NULL && elf_ehdr->e_phnum > 0) {
        long phdr_size;
        phdr_size = (long)elf_ehdr->e_phnum * (long)sizeof(Elf64_Phdr);
        mem_copy(out_buf + out_pos, elf_phdrs, phdr_size);
        out_pos += phdr_size;
    }

    /* Write section data */
    j = 0;
    mem_set(&new_shdrs[0], 0, sizeof(Elf64_Shdr));
    j = 1;
    new_shstrndx = 0;

    for (i = 1; i < elf_ehdr->e_shnum; i++) {
        const Elf64_Shdr *old_sh;
        u64 aligned_pos;

        if (old_to_new[i] < 0) {
            continue;
        }

        old_sh = &elf_shdrs[i];

        /* Track shstrtab */
        if (i == elf_ehdr->e_shstrndx) {
            new_shstrndx = j;
        }

        /* Align */
        if (old_sh->sh_addralign > 1) {
            aligned_pos = align_up((u64)out_pos, old_sh->sh_addralign);
            while ((u64)out_pos < aligned_pos && out_pos < OUT_BUF_SIZE) {
                out_buf[out_pos++] = '\0';
            }
        }

        /* Copy section header */
        mem_copy(&new_shdrs[j], old_sh, sizeof(Elf64_Shdr));

        /* Fix up sh_link */
        if (old_sh->sh_link > 0 &&
            old_sh->sh_link < (u32)elf_ehdr->e_shnum) {
            if (old_to_new[old_sh->sh_link] >= 0) {
                new_shdrs[j].sh_link = (u32)old_to_new[old_sh->sh_link];
            } else {
                new_shdrs[j].sh_link = 0;
            }
        }

        /* Fix up sh_info for relevant section types */
        if ((old_sh->sh_type == SHT_RELA ||
             old_sh->sh_type == SHT_SYMTAB) &&
            old_sh->sh_info > 0 &&
            old_sh->sh_info < (u32)elf_ehdr->e_shnum) {
            if (old_to_new[old_sh->sh_info] >= 0) {
                new_shdrs[j].sh_info = (u32)old_to_new[old_sh->sh_info];
            } else {
                new_shdrs[j].sh_info = 0;
            }
        }

        /* Write section data */
        if (old_sh->sh_type != SHT_NOBITS && old_sh->sh_size > 0) {
            if (out_pos + (long)old_sh->sh_size > OUT_BUF_SIZE) {
                die("output too large");
            }
            new_shdrs[j].sh_offset = (u64)out_pos;
            mem_copy(out_buf + out_pos,
                     elf_data + old_sh->sh_offset,
                     (long)old_sh->sh_size);
            out_pos += (long)old_sh->sh_size;
        } else {
            new_shdrs[j].sh_offset = (u64)out_pos;
        }

        j++;
    }

    new_shnum = j;

    /* Align before section header table */
    while (out_pos & 7) {
        out_buf[out_pos++] = '\0';
    }

    /* Write section headers */
    shdr_pos = out_pos;
    mem_copy(out_buf + out_pos, new_shdrs,
             (long)new_shnum * (long)sizeof(Elf64_Shdr));
    out_pos += (long)new_shnum * (long)sizeof(Elf64_Shdr);

    /* Write ELF header */
    mem_copy(&new_ehdr, elf_ehdr, sizeof(Elf64_Ehdr));
    new_ehdr.e_shoff = (u64)shdr_pos;
    new_ehdr.e_shnum = (u16)new_shnum;
    new_ehdr.e_shstrndx = (u16)new_shstrndx;
    if (elf_phdrs != NULL && elf_ehdr->e_phnum > 0) {
        new_ehdr.e_phoff = (u64)sizeof(Elf64_Ehdr);
    }
    mem_copy(out_buf, &new_ehdr, sizeof(Elf64_Ehdr));

    write_file(output_path, out_buf, out_pos);
}

/* ---- entry point ---- */
int main(int argc, char **argv)
{
    int mode;
    const char *output_path;
    const char *input_path;
    int i;
    const char *arg;

    mode = STRIP_ALL;
    output_path = NULL;
    input_path = NULL;

    /* Handle --version early */
    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "--version")) {
            write_str(1, "GNU strip (free-strip) 2.42\n");
            sys_exit(0);
        }
    }

    for (i = 1; i < argc; i++) {
        arg = argv[i];

        if (streq(arg, "--strip-all")) {
            mode = STRIP_ALL;
        } else if (streq(arg, "--strip-debug") ||
                   streq(arg, "-g")) {
            mode = STRIP_DEBUG;
        } else if (streq(arg, "-o")) {
            if (i + 1 >= argc) {
                die("-o requires an argument");
            }
            i++;
            output_path = argv[i];
        } else if (arg[0] == '-') {
            write_str(2, "free-strip: unknown option '");
            write_str(2, arg);
            write_str(2, "'\n");
            sys_exit(1);
        } else {
            input_path = arg;
        }
    }

    if (input_path == NULL) {
        write_str(2, "Usage: free-strip [--strip-debug] "
                  "[--strip-all] [-o output] file\n");
        sys_exit(1);
    }

    /* Default: modify in place */
    if (output_path == NULL) {
        output_path = input_path;
    }

    /* Read input ELF */
    elf_size = read_file(input_path, file_buf, BUF_SIZE);
    elf_data = file_buf;
    parse_elf();

    /* Strip and write */
    do_strip(output_path, mode);

    sys_exit(0);
    return 0;
}
