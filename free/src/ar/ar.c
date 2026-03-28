/*
 * ar.c - Static archive tool for the free toolchain
 * Usage: free-ar rcs libfoo.a foo.o bar.o
 *        free-ar t libfoo.a
 *        free-ar x libfoo.a
 *        free-ar rcsD libfoo.a foo.o bar.o    (deterministic mode)
 *        free-ar rcsT libfoo.a foo.o bar.o    (thin archive)
 * Pure C89, freestanding-friendly with OS syscalls
 */

#include "../../include/free.h"
#include "../../include/elf.h"

/* ---- syscall wrappers (via __syscall from syscall.S) ---- */

extern long __syscall(long, long, long, long, long, long, long);

#define SYS_OPENAT     56
#define SYS_CLOSE      57
#define SYS_LSEEK      62
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

static long sys_lseek(int fd, long offset, int whence)
{
    return __syscall(SYS_LSEEK, (long)fd, offset, (long)whence, 0, 0, 0);
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
#define SEEK_SET   0
#define SEEK_END   2

#define MAX_MEMBERS  256
#define MAX_SYMS     4096
#define BUF_SIZE     (16 * 1024 * 1024)
#define NAME_MAX_LEN 256
#define SYM_NAME_MAX  512

/* Thin archive magic */
#define THIN_AR_MAGIC "!<thin>\n"
#define THIN_AR_MAGIC_LEN 8

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
    write_str(2, "free-ar: ");
    write_str(2, msg);
    write_str(2, "\n");
    sys_exit(1);
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

static int mem_cmp(const void *a, const void *b, long n)
{
    const unsigned char *pa;
    const unsigned char *pb;
    long i;

    pa = (const unsigned char *)a;
    pb = (const unsigned char *)b;
    for (i = 0; i < n; i++) {
        if (pa[i] != pb[i]) {
            return pa[i] - pb[i];
        }
    }
    return 0;
}

/* Format a u32 in decimal into buf, return pointer to start */
static char *fmt_u32(char *buf, int buflen, u32 val)
{
    int pos;

    pos = buflen - 1;
    buf[pos] = '\0';
    if (val == 0) {
        buf[--pos] = '0';
        return &buf[pos];
    }
    while (val > 0 && pos > 0) {
        buf[--pos] = '0' + (val % 10);
        val /= 10;
    }
    return &buf[pos];
}

/* Format a u64 in decimal into buf, return pointer to start */
static char *fmt_u64(char *buf, int buflen, u64 val)
{
    int pos;

    pos = buflen - 1;
    buf[pos] = '\0';
    if (val == 0) {
        buf[--pos] = '0';
        return &buf[pos];
    }
    while (val > 0 && pos > 0) {
        buf[--pos] = '0' + (char)(val % 10);
        val /= 10;
    }
    return &buf[pos];
}

/* Write an ASCII-padded field into dst (pad with spaces) */
static void write_field(char *dst, int field_len, const char *val)
{
    int vlen;
    int i;

    vlen = str_len(val);
    for (i = 0; i < field_len; i++) {
        if (i < vlen) {
            dst[i] = val[i];
        } else {
            dst[i] = ' ';
        }
    }
}

/* Parse an ASCII decimal field (space-terminated) */
static u64 parse_field(const char *s, int len)
{
    u64 val;
    int i;

    val = 0;
    for (i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            break;
        }
        val = val * 10 + (s[i] - '0');
    }
    return val;
}

/* Extract basename from a path */
static const char *ar_basename(const char *path)
{
    const char *p;
    const char *last;

    last = path;
    p = path;
    while (*p) {
        if (*p == '/') {
            last = p + 1;
        }
        p++;
    }
    return last;
}

/* Read entire file into buffer, return bytes read */
static long read_file(const char *path, char *buf, long bufsize)
{
    int fd;
    long total;
    long n;

    fd = (int)sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        write_str(2, "free-ar: cannot open ");
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

/* Get file size without reading */
static long get_file_size(const char *path)
{
    int fd;
    long size;

    fd = (int)sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    size = sys_lseek(fd, 0, SEEK_END);
    sys_close(fd);
    return size;
}

/* Write buffer to file */
static void write_file(const char *path, const char *buf, long size)
{
    int fd;
    long written;
    long n;

    fd = (int)sys_openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        write_str(2, "free-ar: cannot create ");
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

/* ---- member info ---- */
struct member_info {
    const char *name;     /* basename for normal, full path for thin */
    const char *path;     /* full path (for re-reading) */
    const char *data;     /* file contents */
    long size;            /* file size */
};

/* ---- symbol info for ranlib ---- */
struct sym_entry {
    char name[SYM_NAME_MAX]; /* copied symbol name */
    int member_idx;       /* index of member containing this symbol */
};

/* Static buffers */
static char file_buf[BUF_SIZE];
static char out_buf[BUF_SIZE];
static char symtab_buf[BUF_SIZE / 4];
static char extnames_buf[BUF_SIZE / 4];
static struct member_info members[MAX_MEMBERS];
static int num_members;

static struct sym_entry symtab[MAX_SYMS];
static int num_syms;

/* Archive options */
static int opt_deterministic; /* -D flag */
static int opt_thin;          /* -T flag */

static void copy_sym_name(char *dst, const char *src);

/* Collect global symbols from an ELF object for ranlib */
static void collect_symbols(int member_idx, const char *data, long size)
{
    const Elf64_Ehdr *ehdr;
    const Elf64_Shdr *shdrs;
    const Elf64_Shdr *sh;
    const Elf64_Shdr *strsec;
    const Elf64_Sym *sym;
    const char *strtab;
    int i;
    int j;
    int nsyms;
    long strtab_size;
    u64 shend;

    if (size < (long)sizeof(Elf64_Ehdr)) {
        return;
    }
    ehdr = (const Elf64_Ehdr *)data;
    if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
        return;
    }

    shend = (u64)ehdr->e_shoff +
            (u64)ehdr->e_shnum * (u64)sizeof(Elf64_Shdr);
    if ((u64)size < shend) {
        return;
    }

    shdrs = (const Elf64_Shdr *)(data + ehdr->e_shoff);

    /* Find symbol table */
    for (i = 0; i < ehdr->e_shnum; i++) {
        sh = &shdrs[i];
        if (sh->sh_type != SHT_SYMTAB) {
            continue;
        }
        if (sh->sh_entsize != sizeof(Elf64_Sym)) {
            continue;
        }
        if (sh->sh_link >= ehdr->e_shnum) {
            continue;
        }
        if ((u64)sh->sh_offset + (u64)sh->sh_size > (u64)size) {
            continue;
        }
        strsec = &shdrs[sh->sh_link];
        if ((u64)strsec->sh_offset + (u64)strsec->sh_size > (u64)size) {
            continue;
        }
        nsyms = (int)(sh->sh_size / sh->sh_entsize);
        strtab = data + strsec->sh_offset;
        strtab_size = (long)strsec->sh_size;

        for (j = 0; j < nsyms; j++) {
            sym = (const Elf64_Sym *)(data + sh->sh_offset) + j;
            if (ELF64_ST_BIND(sym->st_info) == STB_GLOBAL &&
                sym->st_shndx != SHN_UNDEF &&
                sym->st_name < (u32)strtab_size &&
                num_syms < MAX_SYMS) {
                if (strtab[sym->st_name] == '\0') {
                    continue;
                }
                copy_sym_name(symtab[num_syms].name, strtab + sym->st_name);
                symtab[num_syms].member_idx = member_idx;
                num_syms++;
            }
        }
    }
}

/* Write a big-endian u32 (for symbol table) */
static void write_be32(char *p, u32 val)
{
    p[0] = (char)((val >> 24) & 0xff);
    p[1] = (char)((val >> 16) & 0xff);
    p[2] = (char)((val >> 8) & 0xff);
    p[3] = (char)(val & 0xff);
}

static void copy_sym_name(char *dst, const char *src)
{
    int i;

    i = 0;
    while (i < SYM_NAME_MAX - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Build the archive symbol table ("/" member) */
static long build_symtab(char *buf, u32 *member_offsets)
{
    long pos;
    int i;
    int name_len;

    pos = 0;
    write_be32(buf, (u32)num_syms);
    pos += 4;

    for (i = 0; i < num_syms; i++) {
        write_be32(buf + pos, member_offsets[symtab[i].member_idx]);
        pos += 4;
    }

    for (i = 0; i < num_syms; i++) {
        name_len = str_len(symtab[i].name);
        mem_copy(buf + pos, symtab[i].name, name_len + 1);
        pos += name_len + 1;
    }

    return pos;
}

/* Build extended names section ("//" member) if needed */
static long build_extnames(char *buf, int *name_offsets)
{
    long pos;
    int i;
    int nlen;

    pos = 0;
    for (i = 0; i < num_members; i++) {
        nlen = str_len(members[i].name);
        if (nlen >= 16) {
            name_offsets[i] = (int)pos;
            mem_copy(buf + pos, members[i].name, nlen);
            pos += nlen;
            buf[pos++] = '/';
            buf[pos++] = '\n';
        } else {
            name_offsets[i] = -1;
        }
    }
    return pos;
}

/* ---- create archive ---- */
static void cmd_create(const char *arname, int argc, const char **argv)
{
    int i;
    long pos;
    long fsize;
    Ar_hdr hdr;
    char numbuf[32];
    char *numstr;
    long symtab_data_size;
    long extnames_size;
    int name_offsets[MAX_MEMBERS];
    u32 member_offsets[MAX_MEMBERS];
    int nlen;
    long member_data_start;
    long hdr_name_len;
    const char *ar_date;
    const char *ar_uid;
    const char *ar_gid;

    /* Deterministic mode: zero timestamps/uid/gid */
    ar_date = opt_deterministic ? "0" : "0";
    ar_uid = opt_deterministic ? "0" : "0";
    ar_gid = opt_deterministic ? "0" : "0";

    /* Read all input files */
    num_members = 0;
    num_syms = 0;
    for (i = 0; i < argc; i++) {
        if (num_members >= MAX_MEMBERS) {
            die("too many members");
        }
        members[num_members].name = ar_basename(argv[i]);
        members[num_members].path = argv[i];
        fsize = read_file(argv[i], file_buf, BUF_SIZE);
        members[num_members].data = (const char *)0;
        members[num_members].size = fsize;

        /* Collect symbols from ELF files */
        collect_symbols(num_members, file_buf, fsize);
        num_members++;
    }

    /* Build extended names section */
    extnames_size = build_extnames(extnames_buf, name_offsets);

    /* For thin archives, member names go in extended names if needed */
    if (opt_thin) {
        /* Thin archives store full paths in extended names */
        extnames_size = 0;
        for (i = 0; i < num_members; i++) {
            int plen;
            plen = str_len(members[i].path);
            name_offsets[i] = (int)extnames_size;
            mem_copy(extnames_buf + extnames_size, members[i].path, plen);
            extnames_size += plen;
            extnames_buf[extnames_size++] = '/';
            extnames_buf[extnames_size++] = '\n';
        }
    }

    /* Estimate symtab data size */
    symtab_data_size = 4;
    symtab_data_size += num_syms * 4;
    for (i = 0; i < num_syms; i++) {
        symtab_data_size += str_len(symtab[i].name) + 1;
    }
    if (symtab_data_size % 2) {
        symtab_data_size++;
    }

    /* Calculate where members start */
    member_data_start = opt_thin ? THIN_AR_MAGIC_LEN : AR_MAGIC_LEN;
    if (num_syms > 0) {
        member_data_start += (long)sizeof(Ar_hdr) + symtab_data_size;
    }
    if (extnames_size > 0) {
        member_data_start += (long)sizeof(Ar_hdr) + extnames_size;
        if (extnames_size % 2) {
            member_data_start++;
        }
    }

    /* Compute member offsets */
    pos = member_data_start;
    for (i = 0; i < num_members; i++) {
        member_offsets[i] = (u32)pos;
        if (opt_thin) {
            /* Thin archive: header only, no file data */
            pos += (long)sizeof(Ar_hdr);
        } else {
            fsize = get_file_size(argv[i]);
            if (fsize < 0) {
                fsize = 0;
            }
            pos += (long)sizeof(Ar_hdr) + fsize;
            if (fsize % 2) {
                pos++;
            }
        }
    }

    /* Now build final symtab with correct offsets */
    if (num_syms > 0) {
        num_syms = 0;
        for (i = 0; i < num_members; i++) {
            fsize = read_file(argv[i], file_buf, BUF_SIZE);
            collect_symbols(i, file_buf, fsize);
        }
        symtab_data_size = build_symtab(symtab_buf, member_offsets);
    }

    /* Build output archive */
    pos = 0;

    /* AR magic */
    if (opt_thin) {
        mem_copy(out_buf + pos, THIN_AR_MAGIC, THIN_AR_MAGIC_LEN);
        pos += THIN_AR_MAGIC_LEN;
    } else {
        mem_copy(out_buf + pos, AR_MAGIC, AR_MAGIC_LEN);
        pos += AR_MAGIC_LEN;
    }

    /* Symbol table member */
    if (num_syms > 0) {
        mem_set(&hdr, ' ', sizeof(Ar_hdr));
        write_field(hdr.ar_name, 16, "/");
        write_field(hdr.ar_date, 12, ar_date);
        write_field(hdr.ar_uid, 6, ar_uid);
        write_field(hdr.ar_gid, 6, ar_gid);
        write_field(hdr.ar_mode, 8, "100644");
        numstr = fmt_u64(numbuf, sizeof(numbuf), (u64)symtab_data_size);
        write_field(hdr.ar_size, 10, numstr);
        hdr.ar_fmag[0] = '`';
        hdr.ar_fmag[1] = '\n';
        mem_copy(out_buf + pos, &hdr, sizeof(Ar_hdr));
        pos += (long)sizeof(Ar_hdr);
        mem_copy(out_buf + pos, symtab_buf, symtab_data_size);
        pos += symtab_data_size;
        if (symtab_data_size % 2) {
            out_buf[pos++] = '\n';
        }
    }

    /* Extended names member */
    if (extnames_size > 0) {
        mem_set(&hdr, ' ', sizeof(Ar_hdr));
        write_field(hdr.ar_name, 16, "//");
        write_field(hdr.ar_date, 12, ar_date);
        write_field(hdr.ar_uid, 6, ar_uid);
        write_field(hdr.ar_gid, 6, ar_gid);
        write_field(hdr.ar_mode, 8, "100644");
        numstr = fmt_u64(numbuf, sizeof(numbuf), (u64)extnames_size);
        write_field(hdr.ar_size, 10, numstr);
        hdr.ar_fmag[0] = '`';
        hdr.ar_fmag[1] = '\n';
        mem_copy(out_buf + pos, &hdr, sizeof(Ar_hdr));
        pos += (long)sizeof(Ar_hdr);
        mem_copy(out_buf + pos, extnames_buf, extnames_size);
        pos += extnames_size;
        if (extnames_size % 2) {
            out_buf[pos++] = '\n';
        }
    }

    /* Member entries */
    for (i = 0; i < num_members; i++) {
        if (opt_thin) {
            /* Thin archive: header only, size=0, name references path */
            fsize = get_file_size(argv[i]);
            if (fsize < 0) {
                fsize = 0;
            }

            mem_set(&hdr, ' ', sizeof(Ar_hdr));
            /* Name: /offset into extended names */
            {
                char name_ref[20];
                int rlen;
                name_ref[0] = '/';
                numstr = fmt_u32(numbuf, sizeof(numbuf),
                                 (u32)name_offsets[i]);
                rlen = str_len(numstr);
                mem_copy(name_ref + 1, numstr, rlen);
                name_ref[1 + rlen] = '\0';
                write_field(hdr.ar_name, 16, name_ref);
            }
            write_field(hdr.ar_date, 12, ar_date);
            write_field(hdr.ar_uid, 6, ar_uid);
            write_field(hdr.ar_gid, 6, ar_gid);
            write_field(hdr.ar_mode, 8, "100644");
            numstr = fmt_u64(numbuf, sizeof(numbuf), (u64)fsize);
            write_field(hdr.ar_size, 10, numstr);
            hdr.ar_fmag[0] = '`';
            hdr.ar_fmag[1] = '\n';

            mem_copy(out_buf + pos, &hdr, sizeof(Ar_hdr));
            pos += (long)sizeof(Ar_hdr);
            /* No data for thin archives */
        } else {
            /* Normal archive: header + file data */
            fsize = read_file(argv[i], file_buf, BUF_SIZE);

            mem_set(&hdr, ' ', sizeof(Ar_hdr));
            nlen = str_len(members[i].name);
            if (name_offsets[i] >= 0) {
                char name_ref[20];
                int rlen;
                name_ref[0] = '/';
                numstr = fmt_u32(numbuf, sizeof(numbuf),
                                 (u32)name_offsets[i]);
                rlen = str_len(numstr);
                mem_copy(name_ref + 1, numstr, rlen);
                name_ref[1 + rlen] = '\0';
                write_field(hdr.ar_name, 16, name_ref);
            } else {
                hdr_name_len = (nlen < 15) ? nlen : 15;
                mem_copy(hdr.ar_name, members[i].name, hdr_name_len);
                hdr.ar_name[hdr_name_len] = '/';
            }
            write_field(hdr.ar_date, 12, ar_date);
            write_field(hdr.ar_uid, 6, ar_uid);
            write_field(hdr.ar_gid, 6, ar_gid);
            write_field(hdr.ar_mode, 8, "100644");
            numstr = fmt_u64(numbuf, sizeof(numbuf), (u64)fsize);
            write_field(hdr.ar_size, 10, numstr);
            hdr.ar_fmag[0] = '`';
            hdr.ar_fmag[1] = '\n';

            mem_copy(out_buf + pos, &hdr, sizeof(Ar_hdr));
            pos += (long)sizeof(Ar_hdr);
            mem_copy(out_buf + pos, file_buf, fsize);
            pos += fsize;
            if (fsize % 2) {
                out_buf[pos++] = '\n';
            }
        }
    }

    write_file(arname, out_buf, pos);
}

/* ---- list archive members ---- */
static void cmd_list(const char *arname)
{
    long arsize;
    long pos;
    const Ar_hdr *hdr;
    u64 member_size;
    char namebuf[NAME_MAX_LEN];
    int nlen;
    int i;
    const char *extnames;
    long extnames_len;
    int is_thin;

    arsize = read_file(arname, file_buf, BUF_SIZE);

    /* Check for thin or regular archive */
    is_thin = 0;
    if (arsize >= THIN_AR_MAGIC_LEN &&
        mem_cmp(file_buf, THIN_AR_MAGIC, THIN_AR_MAGIC_LEN) == 0) {
        is_thin = 1;
        pos = THIN_AR_MAGIC_LEN;
    } else if (arsize >= AR_MAGIC_LEN &&
               mem_cmp(file_buf, AR_MAGIC, AR_MAGIC_LEN) == 0) {
        pos = AR_MAGIC_LEN;
    } else {
        die("not an archive");
    }

    /* First pass: find extended names section */
    extnames = NULL;
    extnames_len = 0;
    {
        long scan_pos;
        scan_pos = pos;
        while (scan_pos + (long)sizeof(Ar_hdr) <= arsize) {
            hdr = (const Ar_hdr *)(file_buf + scan_pos);
            member_size = parse_field(hdr->ar_size, 10);
            if (hdr->ar_name[0] == '/' && hdr->ar_name[1] == '/' &&
                hdr->ar_name[2] == ' ') {
                extnames = file_buf + scan_pos + (long)sizeof(Ar_hdr);
                extnames_len = (long)member_size;
                break;
            }
            scan_pos += (long)sizeof(Ar_hdr);
            if (!is_thin) {
                scan_pos += (long)member_size;
                if (member_size % 2) {
                    scan_pos++;
                }
            }
        }
    }

    /* Second pass: list members */
    while (pos + (long)sizeof(Ar_hdr) <= arsize) {
        hdr = (const Ar_hdr *)(file_buf + pos);
        member_size = parse_field(hdr->ar_size, 10);

        /* Skip special members */
        if (hdr->ar_name[0] == '/' && (hdr->ar_name[1] == ' ' ||
            hdr->ar_name[1] == '/')) {
            pos += (long)sizeof(Ar_hdr);
            if (!is_thin) {
                pos += (long)member_size;
                if (member_size % 2) {
                    pos++;
                }
            }
            continue;
        }

        /* Extract member name */
        if (hdr->ar_name[0] == '/' && hdr->ar_name[1] >= '0' &&
            hdr->ar_name[1] <= '9') {
            u64 offset;
            offset = parse_field(hdr->ar_name + 1, 15);
            nlen = 0;
            if (extnames != NULL) {
                i = (int)offset;
                while (i < extnames_len && extnames[i] != '/' &&
                       extnames[i] != '\n' && nlen < NAME_MAX_LEN - 1) {
                    namebuf[nlen++] = extnames[i++];
                }
            }
            namebuf[nlen] = '\0';
        } else {
            nlen = 0;
            for (i = 0; i < 16; i++) {
                if (hdr->ar_name[i] == '/') {
                    break;
                }
                namebuf[nlen++] = hdr->ar_name[i];
            }
            namebuf[nlen] = '\0';
        }

        write_str(1, namebuf);
        write_str(1, "\n");

        pos += (long)sizeof(Ar_hdr);
        if (!is_thin) {
            pos += (long)member_size;
            if (member_size % 2) {
                pos++;
            }
        }
    }
}

/* ---- extract archive members ---- */
static void cmd_extract(const char *arname)
{
    long arsize;
    long pos;
    const Ar_hdr *hdr;
    u64 member_size;
    char namebuf[NAME_MAX_LEN];
    int nlen;
    int i;
    const char *extnames;
    long extnames_len;

    arsize = read_file(arname, file_buf, BUF_SIZE);
    if (arsize < AR_MAGIC_LEN || mem_cmp(file_buf, AR_MAGIC, AR_MAGIC_LEN) != 0) {
        die("not an archive (thin archives cannot be extracted directly)");
    }

    /* Find extended names section */
    extnames = NULL;
    extnames_len = 0;
    pos = AR_MAGIC_LEN;
    while (pos + (long)sizeof(Ar_hdr) <= arsize) {
        hdr = (const Ar_hdr *)(file_buf + pos);
        member_size = parse_field(hdr->ar_size, 10);
        if (hdr->ar_name[0] == '/' && hdr->ar_name[1] == '/' &&
            hdr->ar_name[2] == ' ') {
            extnames = file_buf + pos + (long)sizeof(Ar_hdr);
            extnames_len = (long)member_size;
            break;
        }
        pos += (long)sizeof(Ar_hdr) + (long)member_size;
        if (member_size % 2) {
            pos++;
        }
    }

    /* Extract members */
    pos = AR_MAGIC_LEN;
    while (pos + (long)sizeof(Ar_hdr) <= arsize) {
        hdr = (const Ar_hdr *)(file_buf + pos);
        member_size = parse_field(hdr->ar_size, 10);

        /* Skip special members */
        if (hdr->ar_name[0] == '/' && (hdr->ar_name[1] == ' ' ||
            hdr->ar_name[1] == '/')) {
            pos += (long)sizeof(Ar_hdr) + (long)member_size;
            if (member_size % 2) {
                pos++;
            }
            continue;
        }

        /* Extract member name */
        if (hdr->ar_name[0] == '/' && hdr->ar_name[1] >= '0' &&
            hdr->ar_name[1] <= '9') {
            u64 offset;
            offset = parse_field(hdr->ar_name + 1, 15);
            nlen = 0;
            if (extnames != NULL) {
                i = (int)offset;
                while (i < extnames_len && extnames[i] != '/' &&
                       extnames[i] != '\n' && nlen < NAME_MAX_LEN - 1) {
                    namebuf[nlen++] = extnames[i++];
                }
            }
            namebuf[nlen] = '\0';
        } else {
            nlen = 0;
            for (i = 0; i < 16; i++) {
                if (hdr->ar_name[i] == '/') {
                    break;
                }
                namebuf[nlen++] = hdr->ar_name[i];
            }
            namebuf[nlen] = '\0';
        }

        /* Write member to file */
        write_str(1, "x - ");
        write_str(1, namebuf);
        write_str(1, "\n");
        write_file(namebuf, file_buf + pos + (long)sizeof(Ar_hdr),
                   (long)member_size);

        pos += (long)sizeof(Ar_hdr) + (long)member_size;
        if (member_size % 2) {
            pos++;
        }
    }
}

/* ---- string comparison ---- */
static int streq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* ---- entry point ---- */
int main(int argc, char **argv)
{
    const char *op;
    int has_r;
    int has_c;
    int has_s;
    int has_t;
    int has_x;
    const char *p;

    /* Handle --version before positional args */
    if (argc >= 2 && streq(argv[1], "--version")) {
        write_str(1, "GNU ar (free-ar) 2.42\n");
        sys_exit(0);
    }

    if (argc < 3) {
        write_str(2, "Usage: free-ar {rcs|rcsD|rcsT|t|x} archive [files...]\n");
        sys_exit(1);
    }

    op = argv[1];
    has_r = 0;
    has_c = 0;
    has_s = 0;
    has_t = 0;
    has_x = 0;
    opt_deterministic = 0;
    opt_thin = 0;
    (void)has_c;
    (void)has_s;
    p = op;
    while (*p) {
        switch (*p) {
        case 'r': has_r = 1; break;
        case 'c': has_c = 1; break;
        case 's': has_s = 1; break;
        case 't': has_t = 1; break;
        case 'x': has_x = 1; break;
        case 'D': opt_deterministic = 1; break;
        case 'T': opt_thin = 1; break;
        default:
            write_str(2, "free-ar: unknown option '");
            sys_write(2, p, 1);
            write_str(2, "'\n");
            sys_exit(1);
        }
        p++;
    }

    if (has_t) {
        cmd_list(argv[2]);
    } else if (has_x) {
        cmd_extract(argv[2]);
    } else if (has_r) {
        if (argc < 4) {
            die("no files to add");
        }
        cmd_create(argv[2], argc - 3, (const char **)(argv + 3));
    } else {
        die("no valid operation specified");
    }

    sys_exit(0);
    return 0;
}
