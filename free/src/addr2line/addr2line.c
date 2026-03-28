/*
 * addr2line.c - Address to source line mapper for the free toolchain
 * Usage: free-addr2line [-e executable] [-f] addr [addr ...]
 * Reads DWARF .debug_line from ELF to map addresses to file:line.
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
#define MAX_FILES  256
#define MAX_LINES  65536

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
    write_str(2, "free-addr2line: ");
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

static void print_dec(u64 val)
{
    char buf[24];
    int pos;

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
    write_str(1, &buf[pos]);
}

/* Parse hex address from string (with or without 0x prefix) */
static u64 parse_hex(const char *s)
{
    u64 val;
    int c;

    val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    while (*s) {
        c = *s;
        if (c >= '0' && c <= '9') {
            val = (val << 4) | (u64)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            val = (val << 4) | (u64)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            val = (val << 4) | (u64)(c - 'A' + 10);
        } else {
            break;
        }
        s++;
    }
    return val;
}

/* ---- DWARF .debug_line parsing ---- */

/* DWARF line number standard opcodes */
#define DW_LNS_copy             1
#define DW_LNS_advance_pc       2
#define DW_LNS_advance_line     3
#define DW_LNS_set_file         4
#define DW_LNS_set_column       5
#define DW_LNS_negate_stmt      6
#define DW_LNS_set_basic_block  7
#define DW_LNS_const_add_pc     8
#define DW_LNS_fixed_advance_pc 9

/* DWARF extended opcodes */
#define DW_LNE_end_sequence     1
#define DW_LNE_set_address      2
#define DW_LNE_define_file      3

/* Line number table entry (accumulated during decode) */
struct line_entry {
    u64 addr;
    int file_idx;
    int line;
};

static char file_buf[BUF_SIZE];
static const char *file_table[MAX_FILES];
static int file_table_count;
static struct line_entry line_table[MAX_LINES];
static int line_table_count;

/* Read a ULEB128 value */
static u64 read_uleb128(const u8 **pp)
{
    u64 result;
    int shift;
    u8 b;

    result = 0;
    shift = 0;
    do {
        b = **pp;
        (*pp)++;
        result |= (u64)(b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    return result;
}

/* Read a SLEB128 value */
static i64 read_sleb128(const u8 **pp)
{
    i64 result;
    int shift;
    u8 b;

    result = 0;
    shift = 0;
    do {
        b = **pp;
        (*pp)++;
        result |= (i64)(b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    if (shift < 64 && (b & 0x40)) {
        result |= -(((i64)1) << shift);
    }
    return result;
}

/* Read file into buffer, return file size */
static long read_file(const char *path, char *buf, long bufsize)
{
    int fd;
    long total;
    long n;

    fd = (int)sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        write_str(2, "free-addr2line: cannot open ");
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

/* Find ELF section by name */
static const Elf64_Shdr *find_section(const Elf64_Ehdr *ehdr,
                                       const Elf64_Shdr *shdrs,
                                       const char *shstrtab,
                                       const char *name)
{
    int i;

    for (i = 0; i < (int)ehdr->e_shnum; i++) {
        if (streq(shstrtab + shdrs[i].sh_name, name)) {
            return &shdrs[i];
        }
    }
    return NULL;
}

/* Parse DWARF v2/v4 .debug_line section */
static void parse_debug_line(const u8 *data, u64 size)
{
    const u8 *end;
    const u8 *p;
    u32 unit_length;
    u16 version;
    u32 header_length;
    u8 min_inst_len;
    u8 default_is_stmt;
    i8 line_base;
    u8 line_range;
    u8 opcode_base;
    const u8 *unit_end;
    u64 addr;
    int file_idx;
    int line;
    int is_stmt;
    int i;

    end = data + size;
    p = data;
    file_table_count = 0;
    line_table_count = 0;

    while (p < end) {
        /* unit_length */
        unit_length = *(const u32 *)p;
        p += 4;
        unit_end = p + unit_length;

        /* version */
        version = *(const u16 *)p;
        p += 2;
        (void)version;

        /* header_length */
        header_length = *(const u32 *)p;
        p += 4;
        /* skip past header to program start */
        (void)(p + header_length);

        /* minimum_instruction_length */
        min_inst_len = *p++;

        /* DWARF 4: max_ops_per_instruction */
        if (version >= 4) {
            p++; /* skip max_ops_per_instruction */
        }

        /* default_is_stmt */
        default_is_stmt = *p++;

        /* line_base */
        line_base = (i8)*p++;

        /* line_range */
        line_range = *p++;

        /* opcode_base */
        opcode_base = *p++;

        /* standard_opcode_lengths */
        for (i = 1; i < (int)opcode_base; i++) {
            p++; /* skip each length */
        }

        /* include_directories (null-terminated strings, ends with empty) */
        while (*p != '\0') {
            while (*p != '\0') {
                p++;
            }
            p++; /* skip NUL */
        }
        p++; /* skip final NUL */

        /* file_names (each: name\0, dir_idx, time, size) */
        while (*p != '\0') {
            if (file_table_count < MAX_FILES) {
                file_table[file_table_count++] = (const char *)p;
            }
            while (*p != '\0') {
                p++;
            }
            p++; /* skip name NUL */
            read_uleb128(&p); /* dir_index */
            read_uleb128(&p); /* time */
            read_uleb128(&p); /* size */
        }
        p++; /* skip final NUL */

        /* line number program */
        addr = 0;
        file_idx = 1;
        line = 1;
        is_stmt = default_is_stmt;

        while (p < unit_end) {
            u8 op;

            op = *p++;

            if (op == 0) {
                /* extended opcode */
                u64 ext_len;
                u8 ext_op;
                const u8 *ext_end;

                ext_len = read_uleb128(&p);
                ext_end = p + ext_len;
                ext_op = *p++;

                if (ext_op == DW_LNE_end_sequence) {
                    /* emit final row */
                    if (line_table_count < MAX_LINES) {
                        line_table[line_table_count].addr = addr;
                        line_table[line_table_count].file_idx = file_idx;
                        line_table[line_table_count].line = line;
                        line_table_count++;
                    }
                    /* reset state */
                    addr = 0;
                    file_idx = 1;
                    line = 1;
                    is_stmt = default_is_stmt;
                } else if (ext_op == DW_LNE_set_address) {
                    addr = *(const u64 *)p;
                } else if (ext_op == DW_LNE_define_file) {
                    if (file_table_count < MAX_FILES) {
                        file_table[file_table_count++] = (const char *)p;
                    }
                }
                p = ext_end;
            } else if (op < opcode_base) {
                /* standard opcode */
                switch (op) {
                case DW_LNS_copy:
                    if (line_table_count < MAX_LINES) {
                        line_table[line_table_count].addr = addr;
                        line_table[line_table_count].file_idx = file_idx;
                        line_table[line_table_count].line = line;
                        line_table_count++;
                    }
                    break;
                case DW_LNS_advance_pc:
                    addr += read_uleb128(&p) * min_inst_len;
                    break;
                case DW_LNS_advance_line:
                    line += (int)read_sleb128(&p);
                    break;
                case DW_LNS_set_file:
                    file_idx = (int)read_uleb128(&p);
                    break;
                case DW_LNS_set_column:
                    read_uleb128(&p); /* ignore column */
                    break;
                case DW_LNS_negate_stmt:
                    is_stmt = !is_stmt;
                    break;
                case DW_LNS_set_basic_block:
                    break;
                case DW_LNS_const_add_pc:
                    addr += ((u64)(255 - opcode_base) / line_range)
                            * min_inst_len;
                    break;
                case DW_LNS_fixed_advance_pc:
                    addr += *(const u16 *)p;
                    p += 2;
                    break;
                default:
                    /* skip unknown standard opcode args */
                    break;
                }
            } else {
                /* special opcode */
                int adjusted;

                adjusted = (int)op - (int)opcode_base;
                addr += ((u64)(adjusted / line_range)) * min_inst_len;
                line += line_base + (adjusted % line_range);

                if (line_table_count < MAX_LINES) {
                    line_table[line_table_count].addr = addr;
                    line_table[line_table_count].file_idx = file_idx;
                    line_table[line_table_count].line = line;
                    line_table_count++;
                }
            }
        }
        (void)is_stmt;
    }
}

/* Look up an address in the line table.
 * Returns the best matching entry index, or -1. */
static int lookup_addr(u64 addr)
{
    int best;
    int i;

    best = -1;
    for (i = 0; i < line_table_count; i++) {
        if (line_table[i].addr <= addr) {
            if (best < 0 || line_table[i].addr > line_table[best].addr) {
                best = i;
            }
        }
    }
    return best;
}

/* Find function name from .symtab for a given address */
static const char *find_func_name(const Elf64_Ehdr *ehdr,
                                   const Elf64_Shdr *shdrs,
                                   const char *elf_data,
                                   u64 addr)
{
    int i;

    for (i = 0; i < (int)ehdr->e_shnum; i++) {
        if (shdrs[i].sh_type == SHT_SYMTAB) {
            const Elf64_Sym *syms;
            const char *strtab;
            int nsyms;
            int j;

            syms = (const Elf64_Sym *)(elf_data + shdrs[i].sh_offset);
            strtab = elf_data + shdrs[shdrs[i].sh_link].sh_offset;
            nsyms = (int)(shdrs[i].sh_size / sizeof(Elf64_Sym));

            for (j = 0; j < nsyms; j++) {
                if (ELF64_ST_TYPE(syms[j].st_info) == STT_FUNC &&
                    addr >= syms[j].st_value &&
                    addr < syms[j].st_value + syms[j].st_size) {
                    return strtab + syms[j].st_name;
                }
            }
        }
    }
    return NULL;
}

/* ---- main ---- */

void _start(void);

int main(int argc, char **argv)
{
    const char *exe_path;
    int show_func;
    int ai;
    long filesz;
    const Elf64_Ehdr *ehdr;
    const Elf64_Shdr *shdrs;
    const char *shstrtab;
    const Elf64_Shdr *debug_line;

    exe_path = "a.out";
    show_func = 0;

    /* Handle --version early */
    for (ai = 1; ai < argc; ai++) {
        if (streq(argv[ai], "--version")) {
            write_str(1, "GNU addr2line (free-addr2line) 2.42\n");
            sys_exit(0);
        }
    }

    /* parse arguments */
    ai = 1;
    while (ai < argc) {
        if (streq(argv[ai], "-e") && ai + 1 < argc) {
            ai++;
            exe_path = argv[ai];
        } else if (streq(argv[ai], "-f")) {
            show_func = 1;
        } else if (streq(argv[ai], "-h") || streq(argv[ai], "--help")) {
            write_str(1, "Usage: free-addr2line [-e exe] [-f] addr ...\n");
            sys_exit(0);
        } else if (argv[ai][0] != '-') {
            break;
        } else {
            write_str(2, "free-addr2line: unknown option: ");
            write_str(2, argv[ai]);
            write_str(2, "\n");
            sys_exit(1);
        }
        ai++;
    }

    if (ai >= argc) {
        write_str(2, "Usage: free-addr2line [-e exe] [-f] addr ...\n");
        sys_exit(1);
    }

    /* read ELF file */
    filesz = read_file(exe_path, file_buf, BUF_SIZE);
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

    /* find .debug_line section */
    debug_line = find_section(ehdr, shdrs, shstrtab, ".debug_line");
    if (debug_line == NULL) {
        die("no .debug_line section (compile with -g)");
    }

    /* parse DWARF line number program */
    parse_debug_line((const u8 *)(file_buf + debug_line->sh_offset),
                     debug_line->sh_size);

    /* look up each address */
    while (ai < argc) {
        u64 addr;
        int idx;

        addr = parse_hex(argv[ai]);
        idx = lookup_addr(addr);

        if (show_func) {
            const char *fname;

            fname = find_func_name(ehdr, shdrs, file_buf, addr);
            if (fname != NULL) {
                write_str(1, fname);
            } else {
                write_str(1, "??");
            }
            write_str(1, "\n");
        }

        if (idx >= 0) {
            int fi;

            fi = line_table[idx].file_idx - 1;
            if (fi >= 0 && fi < file_table_count) {
                write_str(1, file_table[fi]);
            } else {
                write_str(1, "??");
            }
            write_str(1, ":");
            print_dec((u64)line_table[idx].line);
        } else {
            write_str(1, "??:0");
        }
        write_str(1, "\n");

        ai++;
    }

    sys_exit(0);
    return 0;
}
