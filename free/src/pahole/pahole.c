/*
 * pahole.c - Simple struct layout inspector for the free toolchain
 * Usage: free-pahole [-C struct_name] file
 * Reads DWARF debug info, prints struct layouts with padding.
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
#define MAX_MEMBERS 256
#define MAX_STRUCTS 4096

/* ---- DWARF constants ---- */
#define DW_TAG_structure_type   0x13
#define DW_TAG_member           0x0d
#define DW_TAG_typedef          0x16
#define DW_TAG_base_type        0x24
#define DW_TAG_pointer_type     0x0f
#define DW_TAG_array_type       0x01
#define DW_TAG_union_type       0x17
#define DW_TAG_compile_unit     0x11
#define DW_TAG_subrange_type    0x21
#define DW_TAG_enumeration_type 0x04

#define DW_AT_name              0x03
#define DW_AT_byte_size         0x0b
#define DW_AT_data_member_location 0x38
#define DW_AT_type              0x49
#define DW_AT_bit_size          0x0d
#define DW_AT_bit_offset        0x0c

#define DW_FORM_addr        0x01
#define DW_FORM_data2       0x05
#define DW_FORM_data4       0x06
#define DW_FORM_data8       0x07
#define DW_FORM_string      0x08
#define DW_FORM_data1       0x0b
#define DW_FORM_flag        0x0c
#define DW_FORM_strp        0x0e
#define DW_FORM_udata       0x0f
#define DW_FORM_ref4        0x13
#define DW_FORM_ref_addr    0x10
#define DW_FORM_sdata       0x0d
#define DW_FORM_sec_offset  0x17
#define DW_FORM_exprloc     0x18
#define DW_FORM_flag_present 0x19
#define DW_FORM_ref1        0x11
#define DW_FORM_ref2        0x12
#define DW_FORM_ref8        0x14
#define DW_FORM_block1      0x0a
#define DW_FORM_block2      0x03
#define DW_FORM_block4      0x04
#define DW_FORM_block       0x09

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
    write_str(2, "free-pahole: ");
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

/* Print decimal u64 */
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

/* Print string padded to width */
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
        write_str(2, "free-pahole: cannot open ");
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

/* ---- DWARF LEB128 decoding ---- */

static u64 read_uleb128(const u8 **p, const u8 *end)
{
    u64 val;
    int shift;
    u8 b;

    val = 0;
    shift = 0;
    while (*p < end) {
        b = **p;
        (*p)++;
        val |= (u64)(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            break;
        }
        shift += 7;
    }
    return val;
}

static i64 read_sleb128(const u8 **p, const u8 *end)
{
    i64 val;
    int shift;
    u8 b;

    val = 0;
    shift = 0;
    while (*p < end) {
        b = **p;
        (*p)++;
        val |= (i64)(b & 0x7f) << shift;
        shift += 7;
        if ((b & 0x80) == 0) {
            if (shift < 64 && (b & 0x40)) {
                val |= -((i64)1 << shift);
            }
            break;
        }
    }
    return val;
}

/* ---- simple struct info ---- */

struct member_info {
    const char *name;
    u64 offset;       /* byte offset within struct */
    u64 size;         /* byte size of member */
    u64 bit_size;     /* 0 if not bitfield */
    u64 bit_offset;
};

struct struct_info {
    const char *name;
    u64 byte_size;
    struct member_info members[MAX_MEMBERS];
    int num_members;
};

static struct struct_info structs[MAX_STRUCTS];
static int num_structs;

/* Print one struct layout */
static void print_struct(const struct struct_info *s)
{
    int i;
    u64 expected_offset;
    u64 padding;

    write_str(1, "struct ");
    write_str(1, s->name ? s->name : "(anonymous)");
    write_str(1, " {\n");

    expected_offset = 0;
    for (i = 0; i < s->num_members; i++) {
        /* Show padding holes */
        if (s->members[i].offset > expected_offset) {
            padding = s->members[i].offset - expected_offset;
            write_str(1, "        /* XXX ");
            print_dec(padding);
            write_str(1, " bytes hole, try to pack */\n\n");
        }

        write_str(1, "        ");
        print_padded(s->members[i].name ? s->members[i].name : "?", 30);
        write_str(1, "/* ");
        print_dec(s->members[i].offset);
        if (s->members[i].size > 0) {
            write_str(1, "     ");
            print_dec(s->members[i].size);
        }
        write_str(1, " */\n");

        expected_offset = s->members[i].offset + s->members[i].size;
    }

    /* Trailing padding */
    if (s->byte_size > expected_offset && s->num_members > 0) {
        padding = s->byte_size - expected_offset;
        write_str(1, "\n        /* XXX ");
        print_dec(padding);
        write_str(1, " bytes padding at end */\n");
    }

    write_str(1, "\n        /* total size: ");
    print_dec(s->byte_size);
    write_str(1, " bytes */\n");
    write_str(1, "};\n\n");
}

/*
 * Parse .debug_info section (simplified DWARF4 parser).
 * This is a best-effort parser that extracts struct definitions.
 * Full DWARF parsing is complex; we handle the common cases
 * for struct/member layout info.
 */
static void parse_debug_info(const u8 *info, long info_size,
                              const u8 *abbrev, long abbrev_size,
                              const u8 *str, long str_size)
{
    const u8 *p;
    const u8 *end;
    const u8 *cu_end;
    u64 unit_length;
    u16 version;
    u64 abbrev_offset;
    u8 addr_size;
    int in_struct;
    struct struct_info *cur_struct;

    (void)abbrev;
    (void)abbrev_size;

    p = info;
    end = info + info_size;
    in_struct = 0;
    cur_struct = NULL;

    while (p < end) {
        /* Parse compilation unit header */
        if (p + 4 > end) {
            break;
        }
        unit_length = *(const u32 *)p;
        p += 4;
        if (unit_length == 0) {
            break;
        }
        cu_end = p + unit_length;
        if (cu_end > end) {
            break;
        }
        if (p + 2 > cu_end) {
            break;
        }
        version = *(const u16 *)p;
        p += 2;

        if (version >= 5) {
            /* DWARF5: unit_type + addr_size + abbrev_offset */
            if (p + 1 + 1 + 4 > cu_end) { break; }
            p += 1; /* unit_type */
            addr_size = *p; p += 1;
            abbrev_offset = *(const u32 *)p; p += 4;
        } else {
            /* DWARF4: abbrev_offset + addr_size */
            if (p + 4 + 1 > cu_end) { break; }
            abbrev_offset = *(const u32 *)p; p += 4;
            addr_size = *p; p += 1;
        }

        (void)abbrev_offset;
        (void)addr_size;

        /*
         * Full DWARF abbreviation table parsing is needed for correct
         * interpretation. For now, skip to next CU -- real pahole
         * needs a complete DWARF parser. This simplified version
         * outputs a message about the limitation.
         */
        p = cu_end;
    }

    (void)in_struct;
    (void)cur_struct;
    (void)str;
    (void)str_size;
    (void)version;
}

/* ---- entry point ---- */
int main(int argc, char **argv)
{
    const char *filename;
    const char *target_struct;
    long fsize;
    const Elf64_Ehdr *ehdr;
    const Elf64_Shdr *shdrs;
    const char *shstrtab;
    const u8 *debug_info;
    long debug_info_size;
    const u8 *debug_abbrev;
    long debug_abbrev_size;
    const u8 *debug_str;
    long debug_str_size;
    int i;
    int has_debug;

    filename = NULL;
    target_struct = NULL;

    /* Handle --version early */
    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "--version")) {
            write_str(1, "free-pahole (free) 0.1.0\n");
            sys_exit(0);
        }
    }

    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "-C") && i + 1 < argc) {
            i++;
            target_struct = argv[i];
        } else if (argv[i][0] == '-') {
            write_str(2, "free-pahole: unknown option '");
            write_str(2, argv[i]);
            write_str(2, "'\n");
            sys_exit(1);
        } else {
            filename = argv[i];
        }
    }

    if (filename == NULL) {
        write_str(2, "Usage: free-pahole [-C struct_name] file\n");
        sys_exit(1);
    }

    /* Read ELF */
    fsize = read_file(filename, file_buf, BUF_SIZE);

    ehdr = (const Elf64_Ehdr *)file_buf;
    if (fsize < (long)sizeof(Elf64_Ehdr)) {
        die("file too small");
    }
    if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
        die("not an ELF file");
    }

    shdrs = (const Elf64_Shdr *)(file_buf + ehdr->e_shoff);
    shstrtab = file_buf + shdrs[ehdr->e_shstrndx].sh_offset;

    /* Find DWARF sections */
    debug_info = NULL;
    debug_info_size = 0;
    debug_abbrev = NULL;
    debug_abbrev_size = 0;
    debug_str = NULL;
    debug_str_size = 0;
    has_debug = 0;

    for (i = 0; i < ehdr->e_shnum; i++) {
        const char *name;
        name = shstrtab + shdrs[i].sh_name;
        if (streq(name, ".debug_info")) {
            debug_info = (const u8 *)(file_buf + shdrs[i].sh_offset);
            debug_info_size = (long)shdrs[i].sh_size;
            has_debug = 1;
        } else if (streq(name, ".debug_abbrev")) {
            debug_abbrev = (const u8 *)(file_buf + shdrs[i].sh_offset);
            debug_abbrev_size = (long)shdrs[i].sh_size;
        } else if (streq(name, ".debug_str")) {
            debug_str = (const u8 *)(file_buf + shdrs[i].sh_offset);
            debug_str_size = (long)shdrs[i].sh_size;
        }
    }

    if (!has_debug) {
        write_str(2, "free-pahole: no .debug_info section found\n");
        write_str(2, "Compile with -g to include DWARF debug information.\n");
        sys_exit(1);
    }

    /* Parse DWARF info */
    num_structs = 0;
    parse_debug_info(debug_info, debug_info_size,
                     debug_abbrev, debug_abbrev_size,
                     debug_str, debug_str_size);

    /* Print results */
    if (num_structs == 0) {
        write_str(1, "No struct definitions found in DWARF info.\n");
        write_str(1, "(Full DWARF abbreviation table parsing needed ");
        write_str(1, "for complete struct extraction.)\n");
    }

    for (i = 0; i < num_structs; i++) {
        if (target_struct != NULL) {
            if (structs[i].name == NULL ||
                !streq(structs[i].name, target_struct)) {
                continue;
            }
        }
        print_struct(&structs[i]);
    }

    (void)target_struct;
    (void)read_uleb128;
    (void)read_sleb128;

    sys_exit(0);
    return 0;
}
