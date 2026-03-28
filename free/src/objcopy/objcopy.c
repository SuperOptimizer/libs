/*
 * objcopy.c - Binary manipulation tool for the free toolchain
 * Usage: free-objcopy [-O format] [-R section] [-j section]
 *                     [--strip-debug] [--add-section name=file]
 *                     [--set-section-flags name=flags]
 *                     [--prefix-symbols=PREFIX]
 *                     [--redefine-sym OLD=NEW]
 *                     [--localize-symbol=SYM] [--globalize-symbol=SYM]
 *                     [--weaken-symbol=SYM] [--keep-symbol=SYM]
 *                     [-x] [--discard-all]
 *                     [-I format] [-B arch]
 *                     [--change-section-address name+offset]
 *                     input output
 * Pure C89, freestanding with OS syscalls
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
#define SEEK_SET   0
#define SEEK_END   2

#define BUF_SIZE        (16 * 1024 * 1024)
#define OUT_BUF_SIZE    (16 * 1024 * 1024)
#define ADD_BUF_SIZE    (1 * 1024 * 1024)
#define MAX_SECTIONS    256
#define MAX_REMOVE      32
#define MAX_ADD         8
#define MAX_RENAME      64
#define MAX_SYM_OPS     128
#define NAME_MAX_LEN    256

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
    write_str(2, "free-objcopy: ");
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
static char add_buf[ADD_BUF_SIZE];

/* Read entire file */
static long read_file(const char *path, char *buf, long bufsize)
{
    int fd;
    long total;
    long n;

    fd = (int)sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        write_str(2, "free-objcopy: cannot open ");
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
        write_str(2, "free-objcopy: cannot create ");
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

/* ---- options ---- */

#define FMT_ELF    0
#define FMT_BINARY 1

#define INPUT_ELF    0
#define INPUT_BINARY 1

static int output_format;
static int input_format;
static int strip_debug;
static int discard_all;

/* Sections to remove */
static const char *remove_sections[MAX_REMOVE];
static int num_remove;

/* Section to extract (only keep this one) */
static const char *only_section;

/* Sections to add */
struct add_section {
    char name[NAME_MAX_LEN];
    const char *data;
    long size;
};

static struct add_section add_sections[MAX_ADD];
static int num_add;

/* Section flags to set */
struct set_flags {
    char name[NAME_MAX_LEN];
    u64 flags;
};

static struct set_flags set_flags_list[MAX_REMOVE];
static int num_set_flags;

/* Symbol prefix */
static const char *sym_prefix;

/* Symbol renaming */
struct sym_rename {
    char old_name[NAME_MAX_LEN];
    char new_name[NAME_MAX_LEN];
};

static struct sym_rename sym_renames[MAX_RENAME];
static int num_renames;

/* Symbol operations (localize, globalize, weaken, keep) */
#define SYM_OP_LOCALIZE  1
#define SYM_OP_GLOBALIZE 2
#define SYM_OP_WEAKEN    3
#define SYM_OP_KEEP      4

struct sym_op {
    char name[NAME_MAX_LEN];
    int op;
};

static struct sym_op sym_ops[MAX_SYM_OPS];
static int num_sym_ops;
static int has_keep_syms;

/* Section address changes */
struct sec_addr_change {
    char name[NAME_MAX_LEN];
    i64 delta;
};

static struct sec_addr_change sec_addr_changes[MAX_REMOVE];
static int num_sec_addr_changes;

/* Output architecture */
static u16 output_machine;

/* Check if a section should be removed */
static int should_remove(const char *name)
{
    int i;

    for (i = 0; i < num_remove; i++) {
        if (streq(name, remove_sections[i])) {
            return 1;
        }
    }

    if (strip_debug && str_startswith(name, ".debug_")) {
        return 1;
    }

    if (only_section != NULL) {
        if (!streq(name, only_section)) {
            return 1;
        }
    }

    return 0;
}

/* Parse --set-section-flags argument: name=flags */
static u64 parse_section_flags(const char *flags_str)
{
    u64 flags;

    flags = 0;
    while (*flags_str) {
        if (str_startswith(flags_str, "alloc")) {
            flags |= SHF_ALLOC;
            flags_str += 5;
        } else if (str_startswith(flags_str, "load")) {
            flags |= SHF_ALLOC;
            flags_str += 4;
        } else if (str_startswith(flags_str, "readonly")) {
            flags_str += 8;
        } else if (str_startswith(flags_str, "code")) {
            flags |= SHF_EXECINSTR | SHF_ALLOC;
            flags_str += 4;
        } else if (str_startswith(flags_str, "data")) {
            flags |= SHF_ALLOC | SHF_WRITE;
            flags_str += 4;
        } else if (str_startswith(flags_str, "contents")) {
            flags_str += 8;
        } else {
            flags_str++;
        }
        if (*flags_str == ',') {
            flags_str++;
        }
    }
    return flags;
}

/* Check if a symbol should be kept (with -x and --keep-symbol) */
static int should_keep_sym(const char *name, int bind)
{
    int i;

    /* Without -x, keep everything */
    if (!discard_all) {
        return 1;
    }

    /* Always keep global symbols unless specifically modified */
    if (bind == STB_GLOBAL || bind == STB_WEAK) {
        return 1;
    }

    /* Check keep list */
    if (has_keep_syms) {
        for (i = 0; i < num_sym_ops; i++) {
            if (sym_ops[i].op == SYM_OP_KEEP && streq(name, sym_ops[i].name)) {
                return 1;
            }
        }
        return 0;
    }

    /* -x without --keep-symbol: remove all non-global */
    return 0;
}

/* Find a symbol rename */
static const char *find_rename(const char *name)
{
    int i;

    for (i = 0; i < num_renames; i++) {
        if (streq(name, sym_renames[i].old_name)) {
            return sym_renames[i].new_name;
        }
    }
    return NULL;
}

/* Find a symbol operation */
static int find_sym_op(const char *name)
{
    int i;

    for (i = 0; i < num_sym_ops; i++) {
        if ((sym_ops[i].op == SYM_OP_LOCALIZE ||
             sym_ops[i].op == SYM_OP_GLOBALIZE ||
             sym_ops[i].op == SYM_OP_WEAKEN) &&
            streq(name, sym_ops[i].name)) {
            return sym_ops[i].op;
        }
    }
    return 0;
}

/* ---- output: binary format ---- */

static void output_binary(const char *output_path)
{
    long out_pos;
    u64 base_addr;
    u64 end_addr;
    int i;

    out_pos = 0;

    if (elf_phdrs != NULL && elf_ehdr->e_phnum > 0) {
        base_addr = (u64)-1;
        end_addr = 0;
        for (i = 0; i < elf_ehdr->e_phnum; i++) {
            if (elf_phdrs[i].p_type == PT_LOAD) {
                if (elf_phdrs[i].p_paddr < base_addr) {
                    base_addr = elf_phdrs[i].p_paddr;
                }
                if (elf_phdrs[i].p_paddr + elf_phdrs[i].p_filesz > end_addr) {
                    end_addr = elf_phdrs[i].p_paddr + elf_phdrs[i].p_filesz;
                }
            }
        }

        if (base_addr == (u64)-1) {
            die("no LOAD segments found");
        }

        out_pos = (long)(end_addr - base_addr);
        if (out_pos > OUT_BUF_SIZE) {
            die("output too large");
        }
        mem_set(out_buf, 0, out_pos);

        for (i = 0; i < elf_ehdr->e_phnum; i++) {
            if (elf_phdrs[i].p_type == PT_LOAD &&
                elf_phdrs[i].p_filesz > 0) {
                long offset;
                offset = (long)(elf_phdrs[i].p_paddr - base_addr);
                mem_copy(out_buf + offset,
                         elf_data + elf_phdrs[i].p_offset,
                         (long)elf_phdrs[i].p_filesz);
            }
        }
    } else {
        for (i = 1; i < elf_ehdr->e_shnum; i++) {
            const Elf64_Shdr *sh;
            sh = &elf_shdrs[i];

            if (!(sh->sh_flags & SHF_ALLOC)) {
                continue;
            }
            if (sh->sh_type == SHT_NOBITS) {
                continue;
            }
            if (should_remove(section_name(i))) {
                continue;
            }

            if (out_pos + (long)sh->sh_size > OUT_BUF_SIZE) {
                die("output too large");
            }
            mem_copy(out_buf + out_pos,
                     elf_data + sh->sh_offset,
                     (long)sh->sh_size);
            out_pos += (long)sh->sh_size;
        }
    }

    write_file(output_path, out_buf, out_pos);
}

/* ---- input: binary format -> ELF ---- */

static void input_binary_to_elf(const char *input_path,
                                 const char *output_path)
{
    long in_size;
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdrs[5]; /* null, .data, .symtab, .strtab, .shstrtab */
    Elf64_Sym syms[4]; /* null, _binary_start, _binary_end, _binary_size */
    long out_pos;
    long data_off;
    long shstrtab_off;
    long strtab_off;
    long symtab_off;
    long shdr_off;
    const char *shstrtab_data;
    int shstrtab_len;
    const char *strtab_data;
    int strtab_len;
    const char *basename_str;
    const char *p;

    in_size = read_file(input_path, file_buf, BUF_SIZE);

    /* Extract basename for symbol names */
    basename_str = input_path;
    p = input_path;
    while (*p) {
        if (*p == '/') {
            basename_str = p + 1;
        }
        p++;
    }

    /* Build ELF header */
    mem_set(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = output_machine;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = 5;
    ehdr.e_shstrndx = 4;

    /* Layout: ehdr | .data | .shstrtab | .strtab | .symtab | shdrs */
    out_pos = sizeof(Elf64_Ehdr);

    /* .data section (raw binary) */
    data_off = out_pos;
    mem_copy(out_buf + out_pos, file_buf, in_size);
    out_pos += in_size;
    /* Align to 8 */
    while (out_pos & 7) {
        out_buf[out_pos++] = '\0';
    }

    /* .shstrtab */
    shstrtab_off = out_pos;
    shstrtab_data = "\0.data\0.symtab\0.strtab\0.shstrtab\0";
    shstrtab_len = 33;
    mem_copy(out_buf + out_pos, shstrtab_data, shstrtab_len);
    out_pos += shstrtab_len;
    while (out_pos & 7) {
        out_buf[out_pos++] = '\0';
    }

    /* .strtab: \0_binary_start\0_binary_end\0_binary_size\0 */
    strtab_off = out_pos;
    strtab_data = "\0_binary_start\0_binary_end\0_binary_size\0";
    strtab_len = 41;
    mem_copy(out_buf + out_pos, strtab_data, strtab_len);
    out_pos += strtab_len;
    while (out_pos & 7) {
        out_buf[out_pos++] = '\0';
    }

    /* .symtab */
    symtab_off = out_pos;
    mem_set(syms, 0, sizeof(syms));
    /* sym 0: null */
    /* sym 1: _binary_start */
    syms[1].st_name = 1;
    syms[1].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
    syms[1].st_shndx = 1;
    syms[1].st_value = 0;
    /* sym 2: _binary_end */
    syms[2].st_name = 15;
    syms[2].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
    syms[2].st_shndx = 1;
    syms[2].st_value = (u64)in_size;
    /* sym 3: _binary_size */
    syms[3].st_name = 27;
    syms[3].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
    syms[3].st_shndx = SHN_ABS;
    syms[3].st_value = (u64)in_size;

    mem_copy(out_buf + out_pos, syms, sizeof(syms));
    out_pos += (long)sizeof(syms);
    while (out_pos & 7) {
        out_buf[out_pos++] = '\0';
    }

    /* Section headers */
    shdr_off = out_pos;
    mem_set(shdrs, 0, sizeof(shdrs));

    /* [1] .data */
    shdrs[1].sh_name = 1;
    shdrs[1].sh_type = SHT_PROGBITS;
    shdrs[1].sh_flags = SHF_ALLOC;
    shdrs[1].sh_offset = (u64)data_off;
    shdrs[1].sh_size = (u64)in_size;
    shdrs[1].sh_addralign = 1;

    /* [2] .symtab */
    shdrs[2].sh_name = 7;
    shdrs[2].sh_type = SHT_SYMTAB;
    shdrs[2].sh_offset = (u64)symtab_off;
    shdrs[2].sh_size = (u64)sizeof(syms);
    shdrs[2].sh_link = 3;
    shdrs[2].sh_info = 1; /* first global sym */
    shdrs[2].sh_addralign = 8;
    shdrs[2].sh_entsize = sizeof(Elf64_Sym);

    /* [3] .strtab */
    shdrs[3].sh_name = 15;
    shdrs[3].sh_type = SHT_STRTAB;
    shdrs[3].sh_offset = (u64)strtab_off;
    shdrs[3].sh_size = (u64)strtab_len;
    shdrs[3].sh_addralign = 1;

    /* [4] .shstrtab */
    shdrs[4].sh_name = 23;
    shdrs[4].sh_type = SHT_STRTAB;
    shdrs[4].sh_offset = (u64)shstrtab_off;
    shdrs[4].sh_size = (u64)shstrtab_len;
    shdrs[4].sh_addralign = 1;

    mem_copy(out_buf + out_pos, shdrs, sizeof(shdrs));
    out_pos += (long)sizeof(shdrs);

    /* Fix ELF header */
    ehdr.e_shoff = (u64)shdr_off;
    mem_copy(out_buf, &ehdr, sizeof(ehdr));

    write_file(output_path, out_buf, out_pos);

    (void)basename_str;
}

/* ---- output: ELF format ---- */

static u64 align_up(u64 val, u64 align)
{
    if (align == 0) {
        return val;
    }
    return (val + align - 1) & ~(align - 1);
}

/*
 * Rebuild string table with modified symbol names.
 * Returns new strtab size. Writes into dest.
 */
static long rebuild_strtab(char *dest, long dest_size,
                            const char *old_strtab,
                            const Elf64_Sym *old_syms, int nsyms,
                            u32 *new_name_offsets)
{
    long pos;
    int i;
    const char *name;
    const char *renamed;
    int prefix_len;
    int name_len;

    pos = 0;
    dest[pos++] = '\0'; /* null at index 0 */
    prefix_len = sym_prefix ? str_len(sym_prefix) : 0;

    for (i = 0; i < nsyms; i++) {
        name = old_strtab + old_syms[i].st_name;
        if (old_syms[i].st_name == 0 || name[0] == '\0') {
            new_name_offsets[i] = 0;
            continue;
        }

        /* Check for rename */
        renamed = find_rename(name);
        if (renamed != NULL) {
            name = renamed;
        }

        new_name_offsets[i] = (u32)pos;

        /* Add prefix if needed */
        if (sym_prefix != NULL && prefix_len > 0 &&
            ELF64_ST_TYPE(old_syms[i].st_info) != STT_FILE &&
            ELF64_ST_TYPE(old_syms[i].st_info) != STT_SECTION) {
            if (pos + prefix_len >= dest_size) {
                die("strtab overflow");
            }
            mem_copy(dest + pos, sym_prefix, prefix_len);
            pos += prefix_len;
        }

        name_len = str_len(name);
        if (pos + name_len + 1 >= dest_size) {
            die("strtab overflow");
        }
        mem_copy(dest + pos, name, name_len + 1);
        pos += name_len + 1;
    }

    return pos;
}

/* New strtab buffer */
static char new_strtab_buf[BUF_SIZE / 4];
static u32 new_name_offsets[16384];

static void output_elf(const char *output_path)
{
    Elf64_Ehdr new_ehdr;
    Elf64_Shdr new_shdrs[MAX_SECTIONS];
    int old_to_new[MAX_SECTIONS];
    int new_shnum;
    long out_pos;
    int i;
    int j;
    long data_pos;
    int new_shstrndx;

    /* Phase 1: decide which sections to keep */
    new_shnum = 0;
    mem_set(old_to_new, 0, sizeof(old_to_new));

    old_to_new[0] = 0;
    new_shnum = 1;

    for (i = 1; i < elf_ehdr->e_shnum; i++) {
        const char *sname;
        sname = section_name(i);

        if (should_remove(sname)) {
            old_to_new[i] = -1;
            continue;
        }

        old_to_new[i] = new_shnum;
        new_shnum++;
    }

    for (i = 0; i < num_add; i++) {
        new_shnum++;
    }

    if (new_shnum > MAX_SECTIONS) {
        die("too many sections");
    }

    /* Phase 2: build new section headers, write section data */
    out_pos = (long)sizeof(Elf64_Ehdr);

    /* Copy program headers if present */
    if (elf_phdrs != NULL && elf_ehdr->e_phnum > 0) {
        long phdr_size;
        phdr_size = (long)elf_ehdr->e_phnum * (long)sizeof(Elf64_Phdr);
        mem_copy(out_buf + out_pos, elf_phdrs, phdr_size);
        out_pos += phdr_size;
    }

    j = 0;

    /* Section 0: null */
    mem_set(&new_shdrs[0], 0, sizeof(Elf64_Shdr));
    j = 1;

    new_shstrndx = 0;

    for (i = 1; i < elf_ehdr->e_shnum; i++) {
        const Elf64_Shdr *old_sh;
        u64 aligned_pos;
        int k;
        int need_sym_transform;

        if (old_to_new[i] < 0) {
            continue;
        }

        old_sh = &elf_shdrs[i];

        /* Align output position */
        if (old_sh->sh_addralign > 1) {
            aligned_pos = align_up((u64)out_pos, old_sh->sh_addralign);
            while ((u64)out_pos < aligned_pos && out_pos < OUT_BUF_SIZE) {
                out_buf[out_pos++] = '\0';
            }
        }

        /* Copy section header */
        mem_copy(&new_shdrs[j], old_sh, sizeof(Elf64_Shdr));

        /* Track shstrtab */
        if (i == elf_ehdr->e_shstrndx) {
            new_shstrndx = j;
        }

        /* Apply section flag changes */
        for (k = 0; k < num_set_flags; k++) {
            if (streq(section_name(i), set_flags_list[k].name)) {
                new_shdrs[j].sh_flags = set_flags_list[k].flags;
            }
        }

        /* Apply section address changes */
        for (k = 0; k < num_sec_addr_changes; k++) {
            if (streq(section_name(i), sec_addr_changes[k].name)) {
                new_shdrs[j].sh_addr =
                    (u64)((i64)old_sh->sh_addr + sec_addr_changes[k].delta);
            }
        }

        /* Fix up sh_link references */
        if (old_sh->sh_link > 0 &&
            old_sh->sh_link < (u32)elf_ehdr->e_shnum) {
            if (old_to_new[old_sh->sh_link] >= 0) {
                new_shdrs[j].sh_link = (u32)old_to_new[old_sh->sh_link];
            } else {
                new_shdrs[j].sh_link = 0;
            }
        }

        /* Fix up sh_info for REL/RELA/SYMTAB sections */
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

        /* Check if we need symbol table transformation */
        need_sym_transform = 0;
        if (old_sh->sh_type == SHT_SYMTAB &&
            (sym_prefix != NULL || num_renames > 0 ||
             num_sym_ops > 0 || discard_all)) {
            need_sym_transform = 1;
        }

        /* Write section data */
        if (old_sh->sh_type != SHT_NOBITS && old_sh->sh_size > 0) {
            if (out_pos + (long)old_sh->sh_size > OUT_BUF_SIZE) {
                die("output too large");
            }
            new_shdrs[j].sh_offset = (u64)out_pos;

            if (need_sym_transform) {
                /* Transform symbol table in place */
                const Elf64_Sym *old_syms;
                Elf64_Sym *new_syms;
                const char *old_strtab;
                int nsyms;
                int sym_i;
                int out_sym_count;
                const char *sname;
                int op;

                old_syms = (const Elf64_Sym *)(elf_data + old_sh->sh_offset);
                nsyms = (int)(old_sh->sh_size / old_sh->sh_entsize);
                old_strtab = elf_data +
                             elf_shdrs[old_sh->sh_link].sh_offset;

                /* Copy and transform symbols */
                new_syms = (Elf64_Sym *)(out_buf + out_pos);
                out_sym_count = 0;

                for (sym_i = 0; sym_i < nsyms; sym_i++) {
                    Elf64_Sym s;
                    s = old_syms[sym_i];
                    sname = old_strtab + s.st_name;

                    /* Check if symbol should be discarded */
                    if (sym_i > 0 && !should_keep_sym(
                            sname, ELF64_ST_BIND(s.st_info))) {
                        continue;
                    }

                    /* Apply symbol operations */
                    op = find_sym_op(sname);
                    if (op == SYM_OP_LOCALIZE) {
                        s.st_info = ELF64_ST_INFO(
                            STB_LOCAL, ELF64_ST_TYPE(s.st_info));
                    } else if (op == SYM_OP_GLOBALIZE) {
                        s.st_info = ELF64_ST_INFO(
                            STB_GLOBAL, ELF64_ST_TYPE(s.st_info));
                    } else if (op == SYM_OP_WEAKEN) {
                        s.st_info = ELF64_ST_INFO(
                            STB_WEAK, ELF64_ST_TYPE(s.st_info));
                    }

                    new_syms[out_sym_count] = s;
                    out_sym_count++;
                }

                new_shdrs[j].sh_size =
                    (u64)out_sym_count * old_sh->sh_entsize;
                out_pos += (long)new_shdrs[j].sh_size;

                /* Now rebuild strtab if we have renames or prefix */
                if (sym_prefix != NULL || num_renames > 0) {
                    /* Find the linked strtab section */
                    int strtab_idx;
                    strtab_idx = (int)old_sh->sh_link;
                    if (old_to_new[strtab_idx] >= 0) {
                        /* We'll rebuild strtab later when we encounter it */
                    }
                }
            } else if (old_sh->sh_type == SHT_STRTAB &&
                       (sym_prefix != NULL || num_renames > 0)) {
                /* Check if this is a strtab linked from symtab */
                int is_symstrtab;
                int si;
                is_symstrtab = 0;
                for (si = 0; si < elf_ehdr->e_shnum; si++) {
                    if (elf_shdrs[si].sh_type == SHT_SYMTAB &&
                        elf_shdrs[si].sh_link == (u32)i) {
                        is_symstrtab = 1;
                        break;
                    }
                }
                if (is_symstrtab) {
                    /* Rebuild this strtab */
                    const Elf64_Shdr *sym_sh;
                    const Elf64_Sym *syms;
                    int nsyms;
                    long new_size;

                    sym_sh = &elf_shdrs[si];
                    syms = (const Elf64_Sym *)(elf_data + sym_sh->sh_offset);
                    nsyms = (int)(sym_sh->sh_size / sym_sh->sh_entsize);

                    new_size = rebuild_strtab(
                        new_strtab_buf, (long)sizeof(new_strtab_buf),
                        elf_data + old_sh->sh_offset,
                        syms, nsyms, new_name_offsets);

                    if (out_pos + new_size > OUT_BUF_SIZE) {
                        die("output too large");
                    }
                    mem_copy(out_buf + out_pos, new_strtab_buf, new_size);
                    new_shdrs[j].sh_size = (u64)new_size;
                    out_pos += new_size;

                    /* Update symbol name offsets in already-written symtab */
                    /* Find the corresponding output symtab */
                    {
                        int sj;
                        for (sj = 1; sj < j; sj++) {
                            if (new_shdrs[sj].sh_type == SHT_SYMTAB) {
                                Elf64_Sym *osyms;
                                int onsyms;
                                int sk;
                                osyms = (Elf64_Sym *)(out_buf +
                                    new_shdrs[sj].sh_offset);
                                onsyms = (int)(new_shdrs[sj].sh_size /
                                    new_shdrs[sj].sh_entsize);
                                for (sk = 0; sk < onsyms && sk < nsyms;
                                     sk++) {
                                    osyms[sk].st_name =
                                        new_name_offsets[sk];
                                }
                                break;
                            }
                        }
                    }
                } else {
                    mem_copy(out_buf + out_pos,
                             elf_data + old_sh->sh_offset,
                             (long)old_sh->sh_size);
                    out_pos += (long)old_sh->sh_size;
                }
            } else {
                mem_copy(out_buf + out_pos,
                         elf_data + old_sh->sh_offset,
                         (long)old_sh->sh_size);
                out_pos += (long)old_sh->sh_size;
            }
        } else {
            new_shdrs[j].sh_offset = (u64)out_pos;
        }

        j++;
    }

    /* Write added sections */
    for (i = 0; i < num_add; i++) {
        int name_len;
        int k;

        mem_set(&new_shdrs[j], 0, sizeof(Elf64_Shdr));
        new_shdrs[j].sh_type = SHT_PROGBITS;
        new_shdrs[j].sh_addralign = 1;

        for (k = 0; k < num_set_flags; k++) {
            if (streq(add_sections[i].name, set_flags_list[k].name)) {
                new_shdrs[j].sh_flags = set_flags_list[k].flags;
            }
        }

        if (new_shstrndx > 0) {
            Elf64_Shdr *strtab_sh;
            strtab_sh = &new_shdrs[new_shstrndx];
            name_len = str_len(add_sections[i].name);

            new_shdrs[j].sh_name = (u32)strtab_sh->sh_size;

            if ((long)(strtab_sh->sh_offset + strtab_sh->sh_size) +
                name_len + 1 <= OUT_BUF_SIZE) {
                mem_copy(out_buf + strtab_sh->sh_offset + strtab_sh->sh_size,
                         add_sections[i].name, name_len + 1);
                strtab_sh->sh_size += (u64)(name_len + 1);
                if ((long)(strtab_sh->sh_offset + strtab_sh->sh_size) >
                    out_pos) {
                    out_pos = (long)(strtab_sh->sh_offset +
                                    strtab_sh->sh_size);
                }
            }
        }

        if (add_sections[i].size > 0) {
            if (out_pos + add_sections[i].size > OUT_BUF_SIZE) {
                die("output too large");
            }
            new_shdrs[j].sh_offset = (u64)out_pos;
            new_shdrs[j].sh_size = (u64)add_sections[i].size;
            mem_copy(out_buf + out_pos,
                     add_sections[i].data,
                     add_sections[i].size);
            out_pos += add_sections[i].size;
        }

        j++;
    }

    new_shnum = j;

    /* Align to 8 bytes before section headers */
    while (out_pos & 7) {
        out_buf[out_pos++] = '\0';
    }

    /* Phase 3: write section headers */
    data_pos = out_pos;
    mem_copy(out_buf + out_pos, new_shdrs,
             (long)new_shnum * (long)sizeof(Elf64_Shdr));
    out_pos += (long)new_shnum * (long)sizeof(Elf64_Shdr);

    /* Phase 4: write ELF header */
    mem_copy(&new_ehdr, elf_ehdr, sizeof(Elf64_Ehdr));
    new_ehdr.e_shoff = (u64)data_pos;
    new_ehdr.e_shnum = (u16)new_shnum;
    new_ehdr.e_shstrndx = (u16)new_shstrndx;
    if (output_machine != 0) {
        new_ehdr.e_machine = output_machine;
    }
    if (elf_phdrs != NULL && elf_ehdr->e_phnum > 0) {
        new_ehdr.e_phoff = (u64)sizeof(Elf64_Ehdr);
    }
    mem_copy(out_buf, &new_ehdr, sizeof(Elf64_Ehdr));

    write_file(output_path, out_buf, out_pos);
}

/* ---- argument parsing helpers ---- */

static void parse_add_section(const char *arg)
{
    int i;
    int eq_pos;
    long fsize;

    if (num_add >= MAX_ADD) {
        die("too many --add-section arguments");
    }

    eq_pos = -1;
    for (i = 0; arg[i]; i++) {
        if (arg[i] == '=') {
            eq_pos = i;
            break;
        }
    }
    if (eq_pos < 0) {
        die("--add-section requires name=file format");
    }
    if (eq_pos >= NAME_MAX_LEN) {
        die("section name too long");
    }

    mem_copy(add_sections[num_add].name, arg, eq_pos);
    add_sections[num_add].name[eq_pos] = '\0';

    fsize = read_file(arg + eq_pos + 1, add_buf, ADD_BUF_SIZE);
    add_sections[num_add].data = add_buf;
    add_sections[num_add].size = fsize;

    num_add++;
}

static void parse_set_section_flags(const char *arg)
{
    int i;
    int eq_pos;

    if (num_set_flags >= MAX_REMOVE) {
        die("too many --set-section-flags arguments");
    }

    eq_pos = -1;
    for (i = 0; arg[i]; i++) {
        if (arg[i] == '=') {
            eq_pos = i;
            break;
        }
    }
    if (eq_pos < 0) {
        die("--set-section-flags requires name=flags format");
    }
    if (eq_pos >= NAME_MAX_LEN) {
        die("section name too long");
    }

    mem_copy(set_flags_list[num_set_flags].name, arg, eq_pos);
    set_flags_list[num_set_flags].name[eq_pos] = '\0';
    set_flags_list[num_set_flags].flags =
        parse_section_flags(arg + eq_pos + 1);
    num_set_flags++;
}

static void parse_redefine_sym(const char *arg)
{
    int i;
    int eq_pos;

    if (num_renames >= MAX_RENAME) {
        die("too many --redefine-sym arguments");
    }

    eq_pos = -1;
    for (i = 0; arg[i]; i++) {
        if (arg[i] == '=') {
            eq_pos = i;
            break;
        }
    }
    if (eq_pos < 0) {
        die("--redefine-sym requires OLD=NEW format");
    }
    if (eq_pos >= NAME_MAX_LEN) {
        die("symbol name too long");
    }

    mem_copy(sym_renames[num_renames].old_name, arg, eq_pos);
    sym_renames[num_renames].old_name[eq_pos] = '\0';

    i = str_len(arg + eq_pos + 1);
    if (i >= NAME_MAX_LEN) {
        die("symbol name too long");
    }
    mem_copy(sym_renames[num_renames].new_name, arg + eq_pos + 1, i + 1);
    num_renames++;
}

static void add_sym_op(const char *name, int op)
{
    int len;

    if (num_sym_ops >= MAX_SYM_OPS) {
        die("too many symbol operations");
    }

    len = str_len(name);
    if (len >= NAME_MAX_LEN) {
        die("symbol name too long");
    }
    mem_copy(sym_ops[num_sym_ops].name, name, len + 1);
    sym_ops[num_sym_ops].op = op;
    num_sym_ops++;

    if (op == SYM_OP_KEEP) {
        has_keep_syms = 1;
    }
}

static void parse_change_section_addr(const char *arg)
{
    int i;
    int sep_pos;
    i64 delta;
    int sign;
    const char *num;

    if (num_sec_addr_changes >= MAX_REMOVE) {
        die("too many --change-section-address arguments");
    }

    /* Find '+' or '-' separator (after section name) */
    sep_pos = -1;
    for (i = 0; arg[i]; i++) {
        if (arg[i] == '+' || arg[i] == '-') {
            sep_pos = i;
            break;
        }
    }
    if (sep_pos < 0 || sep_pos >= NAME_MAX_LEN) {
        die("--change-section-address requires name+offset format");
    }

    mem_copy(sec_addr_changes[num_sec_addr_changes].name, arg, sep_pos);
    sec_addr_changes[num_sec_addr_changes].name[sep_pos] = '\0';

    sign = (arg[sep_pos] == '-') ? -1 : 1;
    num = arg + sep_pos + 1;

    /* Parse hex or decimal */
    delta = 0;
    if (num[0] == '0' && (num[1] == 'x' || num[1] == 'X')) {
        num += 2;
        while (*num) {
            delta *= 16;
            if (*num >= '0' && *num <= '9') {
                delta += *num - '0';
            } else if (*num >= 'a' && *num <= 'f') {
                delta += *num - 'a' + 10;
            } else if (*num >= 'A' && *num <= 'F') {
                delta += *num - 'A' + 10;
            } else {
                break;
            }
            num++;
        }
    } else {
        while (*num >= '0' && *num <= '9') {
            delta = delta * 10 + (*num - '0');
            num++;
        }
    }
    sec_addr_changes[num_sec_addr_changes].delta = delta * sign;
    num_sec_addr_changes++;
}

/* ---- entry point ---- */
int main(int argc, char **argv)
{
    const char *input_path;
    const char *output_path;
    int i;
    const char *arg;

    output_format = FMT_ELF;
    input_format = INPUT_ELF;
    strip_debug = 0;
    discard_all = 0;
    num_remove = 0;
    num_add = 0;
    num_set_flags = 0;
    num_renames = 0;
    num_sym_ops = 0;
    num_sec_addr_changes = 0;
    has_keep_syms = 0;
    sym_prefix = NULL;
    only_section = NULL;
    input_path = NULL;
    output_path = NULL;
    output_machine = EM_AARCH64;

    /* Handle --version early */
    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "--version")) {
            write_str(1, "GNU objcopy (free-objcopy) 2.42\n");
            sys_exit(0);
        }
    }

    for (i = 1; i < argc; i++) {
        arg = argv[i];

        if (streq(arg, "-O")) {
            if (i + 1 >= argc) {
                die("-O requires an argument");
            }
            i++;
            if (streq(argv[i], "binary")) {
                output_format = FMT_BINARY;
            } else if (streq(argv[i], "elf64-littleaarch64") ||
                       streq(argv[i], "elf64-little")) {
                output_format = FMT_ELF;
            } else {
                write_str(2, "free-objcopy: unknown format '");
                write_str(2, argv[i]);
                write_str(2, "'\n");
                sys_exit(1);
            }
        } else if (streq(arg, "-I")) {
            if (i + 1 >= argc) {
                die("-I requires an argument");
            }
            i++;
            if (streq(argv[i], "binary")) {
                input_format = INPUT_BINARY;
            } else {
                input_format = INPUT_ELF;
            }
        } else if (streq(arg, "-B")) {
            if (i + 1 >= argc) {
                die("-B requires an argument");
            }
            i++;
            if (streq(argv[i], "aarch64") || streq(argv[i], "arm64")) {
                output_machine = EM_AARCH64;
            } else if (streq(argv[i], "i386") || streq(argv[i], "x86")) {
                output_machine = 3;
            } else if (streq(argv[i], "x86-64") ||
                       streq(argv[i], "x86_64")) {
                output_machine = 62;
            } else if (streq(argv[i], "arm")) {
                output_machine = 40;
            }
        } else if (streq(arg, "-R")) {
            if (i + 1 >= argc) {
                die("-R requires an argument");
            }
            i++;
            if (num_remove >= MAX_REMOVE) {
                die("too many -R arguments");
            }
            remove_sections[num_remove++] = argv[i];
        } else if (streq(arg, "-j")) {
            if (i + 1 >= argc) {
                die("-j requires an argument");
            }
            i++;
            only_section = argv[i];
        } else if (streq(arg, "-x") || streq(arg, "--discard-all")) {
            discard_all = 1;
        } else if (streq(arg, "--strip-debug")) {
            strip_debug = 1;
        } else if (str_startswith(arg, "--prefix-symbols=")) {
            sym_prefix = arg + 17;
        } else if (str_startswith(arg, "--prefix-symbols")) {
            if (i + 1 >= argc) {
                die("--prefix-symbols requires an argument");
            }
            i++;
            sym_prefix = argv[i];
        } else if (str_startswith(arg, "--redefine-sym=")) {
            parse_redefine_sym(arg + 15);
        } else if (streq(arg, "--redefine-sym")) {
            if (i + 1 >= argc) {
                die("--redefine-sym requires an argument");
            }
            i++;
            parse_redefine_sym(argv[i]);
        } else if (str_startswith(arg, "--localize-symbol=")) {
            add_sym_op(arg + 18, SYM_OP_LOCALIZE);
        } else if (streq(arg, "--localize-symbol")) {
            if (i + 1 >= argc) {
                die("--localize-symbol requires an argument");
            }
            i++;
            add_sym_op(argv[i], SYM_OP_LOCALIZE);
        } else if (str_startswith(arg, "--globalize-symbol=")) {
            add_sym_op(arg + 19, SYM_OP_GLOBALIZE);
        } else if (streq(arg, "--globalize-symbol")) {
            if (i + 1 >= argc) {
                die("--globalize-symbol requires an argument");
            }
            i++;
            add_sym_op(argv[i], SYM_OP_GLOBALIZE);
        } else if (str_startswith(arg, "--weaken-symbol=")) {
            add_sym_op(arg + 16, SYM_OP_WEAKEN);
        } else if (streq(arg, "--weaken-symbol")) {
            if (i + 1 >= argc) {
                die("--weaken-symbol requires an argument");
            }
            i++;
            add_sym_op(argv[i], SYM_OP_WEAKEN);
        } else if (str_startswith(arg, "--keep-symbol=")) {
            add_sym_op(arg + 14, SYM_OP_KEEP);
        } else if (streq(arg, "--keep-symbol")) {
            if (i + 1 >= argc) {
                die("--keep-symbol requires an argument");
            }
            i++;
            add_sym_op(argv[i], SYM_OP_KEEP);
        } else if (str_startswith(arg, "--change-section-address")) {
            if (arg[24] == '=') {
                parse_change_section_addr(arg + 25);
            } else if (arg[24] == ' ' || arg[24] == '\0') {
                if (i + 1 >= argc) {
                    die("--change-section-address requires an argument");
                }
                i++;
                parse_change_section_addr(argv[i]);
            }
        } else if (str_startswith(arg, "--add-section")) {
            if (arg[13] == '=') {
                parse_add_section(arg + 14);
            } else {
                if (i + 1 >= argc) {
                    die("--add-section requires an argument");
                }
                i++;
                parse_add_section(argv[i]);
            }
        } else if (str_startswith(arg, "--set-section-flags")) {
            if (arg[19] == '=') {
                parse_set_section_flags(arg + 20);
            } else {
                if (i + 1 >= argc) {
                    die("--set-section-flags requires an argument");
                }
                i++;
                parse_set_section_flags(argv[i]);
            }
        } else if (arg[0] == '-') {
            /* Accept unknown options gracefully for Kbuild compat */
        } else if (input_path == NULL) {
            input_path = arg;
        } else if (output_path == NULL) {
            output_path = arg;
        } else {
            die("too many arguments");
        }
    }

    if (input_path == NULL || output_path == NULL) {
        write_str(2, "Usage: free-objcopy [options] input output\n");
        sys_exit(1);
    }

    /* Handle -I binary: wrap raw binary in ELF */
    if (input_format == INPUT_BINARY) {
        input_binary_to_elf(input_path, output_path);
        sys_exit(0);
    }

    /* Read input ELF */
    elf_size = read_file(input_path, file_buf, BUF_SIZE);
    elf_data = file_buf;
    parse_elf();

    /* Produce output */
    if (output_format == FMT_BINARY) {
        output_binary(output_path);
    } else {
        output_elf(output_path);
    }

    sys_exit(0);
    return 0;
}
