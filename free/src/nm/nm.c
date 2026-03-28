/*
 * nm.c - Symbol listing tool for the free toolchain
 * Usage: free-nm [-g] [-u] [-n] [-r] [-p] [-S] [-D] [--defined-only]
 *                [--no-sort] [-B] [-P] [-C] file.o
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
#define MAX_SYMS   16384

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
    write_str(2, "free-nm: ");
    write_str(2, msg);
    write_str(2, "\n");
    sys_exit(1);
}

static int str_cmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int streq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* Print hex nibble */
static char hex_char(int v)
{
    if (v < 10) {
        return '0' + (char)v;
    }
    return 'a' + (char)(v - 10);
}

/* Print a hex value with fixed width */
static void print_hex(u64 val, int width)
{
    char buf[17];
    int i;

    for (i = width - 1; i >= 0; i--) {
        buf[i] = hex_char((int)(val & 0xf));
        val >>= 4;
    }
    buf[width] = '\0';
    write_str(1, buf);
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
        write_str(2, "free-nm: cannot open ");
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

/* ---- ELF parsing ---- */

static const Elf64_Ehdr *elf_ehdr;
static const Elf64_Shdr *elf_shdrs;
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
    elf_shstrtab = elf_data + elf_shdrs[elf_ehdr->e_shstrndx].sh_offset;
}

static const char *section_name(int idx)
{
    if (idx == 0 || idx >= elf_ehdr->e_shnum) {
        return "";
    }
    return elf_shstrtab + elf_shdrs[idx].sh_name;
}

/* ---- symbol type letter determination ---- */

static char sym_type_letter(const Elf64_Sym *sym)
{
    int bind;
    int type;
    u16 shndx;
    const Elf64_Shdr *sh;
    const char *sname;
    char letter;

    bind = ELF64_ST_BIND(sym->st_info);
    type = ELF64_ST_TYPE(sym->st_info);
    shndx = sym->st_shndx;

    /* Undefined */
    if (shndx == SHN_UNDEF) {
        if (bind == STB_WEAK) {
            if (type == STT_OBJECT) {
                return 'v';
            }
            return 'w';
        }
        return 'U';
    }

    /* Absolute */
    if (shndx == SHN_ABS) {
        letter = 'a';
        if (bind == STB_GLOBAL || bind == STB_WEAK) {
            letter = 'A';
        }
        return letter;
    }

    /* Common */
    if (shndx == SHN_COMMON) {
        return 'C';
    }

    /* Weak symbols */
    if (bind == STB_WEAK) {
        if (type == STT_OBJECT) {
            return 'V';
        }
        return 'W';
    }

    /* Determine section type */
    sh = &elf_shdrs[shndx];
    sname = section_name(shndx);

    /* Debug sections */
    if (sname[0] == '.' && sname[1] == 'd' && sname[2] == 'e' &&
        sname[3] == 'b' && sname[4] == 'u' && sname[5] == 'g') {
        letter = 'N';
        if (bind == STB_LOCAL) {
            letter = 'n';
        }
        return letter;
    }

    /* Text (executable) */
    if (sh->sh_flags & SHF_EXECINSTR) {
        letter = 't';
        if (bind == STB_GLOBAL) {
            letter = 'T';
        }
        return letter;
    }

    /* BSS (no bits, writable) */
    if (sh->sh_type == SHT_NOBITS) {
        letter = 'b';
        if (bind == STB_GLOBAL) {
            letter = 'B';
        }
        return letter;
    }

    /* Read-only data */
    if ((sh->sh_flags & SHF_ALLOC) && !(sh->sh_flags & SHF_WRITE)) {
        letter = 'r';
        if (bind == STB_GLOBAL) {
            letter = 'R';
        }
        return letter;
    }

    /* Writable data */
    if (sh->sh_flags & SHF_WRITE) {
        letter = 'd';
        if (bind == STB_GLOBAL) {
            letter = 'D';
        }
        return letter;
    }

    /* Other */
    letter = '?';
    return letter;
}

/* ---- collected symbol for sorting ---- */

struct nm_sym {
    const char *name;
    u64 value;
    u64 size;
    char type_letter;
    int index; /* original order */
};

static struct nm_sym sym_list[MAX_SYMS];
static int num_syms;

/* ---- sorting ---- */

/* Sort mode: 0 = by name, 1 = by address, 2 = no sort */
static int sort_mode;
static int sort_reverse;

static int sym_compare(const struct nm_sym *a, const struct nm_sym *b)
{
    int cmp;

    if (sort_mode == 2) {
        cmp = a->index - b->index;
    } else if (sort_mode == 1) {
        if (a->value < b->value) {
            cmp = -1;
        } else if (a->value > b->value) {
            cmp = 1;
        } else {
            cmp = str_cmp(a->name, b->name);
        }
    } else {
        cmp = str_cmp(a->name, b->name);
    }

    if (sort_reverse) {
        cmp = -cmp;
    }
    return cmp;
}

/* Simple insertion sort */
static void sort_syms(void)
{
    int i;
    int j;
    struct nm_sym tmp;

    if (sort_mode == 2 && !sort_reverse) {
        return;
    }

    for (i = 1; i < num_syms; i++) {
        tmp = sym_list[i];
        j = i - 1;
        while (j >= 0 && sym_compare(&tmp, &sym_list[j]) < 0) {
            sym_list[j + 1] = sym_list[j];
            j--;
        }
        sym_list[j + 1] = tmp;
    }
}

/* ---- output format ---- */

#define FMT_BSD   0
#define FMT_POSIX 1

static int output_format;

/* ---- main logic ---- */

static void list_symbols(int opt_g, int opt_u, int opt_defined_only,
                          int opt_print_size, u32 sh_type)
{
    int i;
    int j;
    const Elf64_Shdr *sh;
    const Elf64_Sym *sym;
    const char *strtab;
    int nsyms;
    const char *name;
    char letter;
    char type_str[2];
    int found_table;

    num_syms = 0;

    /* Check if the requested symbol table section exists */
    found_table = 0;
    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        if (elf_shdrs[i].sh_type == sh_type) {
            found_table = 1;
            break;
        }
    }
    if (!found_table) {
        if (sh_type == SHT_SYMTAB) {
            die("no .symtab section found");
        } else {
            die("no .dynsym section found");
        }
    }

    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];
        if (sh->sh_type != sh_type) {
            continue;
        }

        strtab = elf_data + elf_shdrs[sh->sh_link].sh_offset;
        nsyms = (int)(sh->sh_size / sh->sh_entsize);

        for (j = 0; j < nsyms; j++) {
            sym = (const Elf64_Sym *)(elf_data + sh->sh_offset) + j;

            /* Skip null symbol */
            if (j == 0 && sym->st_name == 0 && sym->st_value == 0 &&
                sym->st_shndx == SHN_UNDEF) {
                continue;
            }

            /* Skip file symbols */
            if (ELF64_ST_TYPE(sym->st_info) == STT_FILE) {
                continue;
            }

            /* Skip section symbols */
            if (ELF64_ST_TYPE(sym->st_info) == STT_SECTION) {
                continue;
            }

            name = strtab + sym->st_name;
            if (name[0] == '\0') {
                continue;
            }

            /* Skip AArch64 mapping symbols ($d, $x, $t, etc.) */
            if (name[0] == '$' && name[1] != '\0' &&
                (name[2] == '\0' || name[2] == '.')) {
                continue;
            }

            letter = sym_type_letter(sym);

            /* -g: only global symbols */
            if (opt_g) {
                if (letter >= 'a' && letter <= 'z' &&
                    letter != 'w' && letter != 'v') {
                    continue;
                }
            }

            /* -u: only undefined symbols */
            if (opt_u) {
                if (letter != 'U' && letter != 'w' && letter != 'v') {
                    continue;
                }
            }

            /* --defined-only: skip undefined symbols */
            if (opt_defined_only) {
                if (letter == 'U' || letter == 'w' || letter == 'v') {
                    continue;
                }
            }

            if (num_syms >= MAX_SYMS) {
                die("too many symbols");
            }

            sym_list[num_syms].name = name;
            sym_list[num_syms].value = sym->st_value;
            sym_list[num_syms].size = sym->st_size;
            sym_list[num_syms].type_letter = letter;
            sym_list[num_syms].index = num_syms;
            num_syms++;
        }
    }

    /* Sort */
    sort_syms();

    /* Print */
    type_str[1] = '\0';
    for (i = 0; i < num_syms; i++) {
        if (output_format == FMT_POSIX) {
            /* POSIX format: name type value size */
            write_str(1, sym_list[i].name);
            write_str(1, " ");
            type_str[0] = sym_list[i].type_letter;
            write_str(1, type_str);
            write_str(1, " ");
            print_hex(sym_list[i].value, 16);
            if (opt_print_size) {
                write_str(1, " ");
                print_hex(sym_list[i].size, 16);
            }
            write_str(1, "\n");
        } else {
            /* BSD format (default) */
            if (sym_list[i].type_letter == 'U' ||
                sym_list[i].type_letter == 'w' ||
                sym_list[i].type_letter == 'v') {
                write_str(1, "                ");
            } else {
                print_hex(sym_list[i].value, 16);
            }

            if (opt_print_size) {
                write_str(1, " ");
                if (sym_list[i].size > 0) {
                    print_hex(sym_list[i].size, 16);
                } else {
                    write_str(1, "                ");
                }
            }

            write_str(1, " ");
            type_str[0] = sym_list[i].type_letter;
            write_str(1, type_str);
            write_str(1, " ");
            write_str(1, sym_list[i].name);
            write_str(1, "\n");
        }
    }
}

/* ---- entry point ---- */
int main(int argc, char **argv)
{
    int opt_g;
    int opt_u;
    int opt_n;
    int opt_r;
    int opt_p;
    int opt_S;
    int opt_D;
    int opt_defined_only;
    const char *filename;
    int i;
    const char *arg;
    const char *p;

    opt_g = 0;
    opt_u = 0;
    opt_n = 0;
    opt_r = 0;
    opt_p = 0;
    opt_S = 0;
    opt_D = 0;
    opt_defined_only = 0;
    output_format = FMT_BSD;
    filename = NULL;

    /* Handle --version early */
    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "--version")) {
            write_str(1, "GNU nm (free-nm) 2.42\n");
            sys_exit(0);
        }
    }

    for (i = 1; i < argc; i++) {
        arg = argv[i];
        if (streq(arg, "--defined-only")) {
            opt_defined_only = 1;
        } else if (streq(arg, "--no-sort")) {
            opt_p = 1;
        } else if (streq(arg, "--format=bsd") || streq(arg, "-B")) {
            output_format = FMT_BSD;
        } else if (streq(arg, "--format=posix") || streq(arg, "-P")) {
            output_format = FMT_POSIX;
        } else if (arg[0] == '-' && arg[1] == '-') {
            /* Skip unknown long options gracefully */
        } else if (arg[0] == '-' && arg[1] != '\0') {
            p = arg + 1;
            while (*p) {
                switch (*p) {
                case 'g': opt_g = 1; break;
                case 'u': opt_u = 1; break;
                case 'n': opt_n = 1; break;
                case 'r': opt_r = 1; break;
                case 'p': opt_p = 1; break;
                case 'S': opt_S = 1; break;
                case 'D': opt_D = 1; break;
                case 'C': break; /* demangle: no-op for C */
                case 'B': output_format = FMT_BSD; break;
                case 'P': output_format = FMT_POSIX; break;
                default:
                    write_str(2, "free-nm: unknown option '-");
                    sys_write(2, p, 1);
                    write_str(2, "'\n");
                    sys_exit(1);
                }
                p++;
            }
        } else {
            filename = arg;
        }
    }

    if (filename == NULL) {
        write_str(2, "Usage: free-nm [-g] [-u] [-n] [-r] [-p] [-S] [-D] ");
        write_str(2, "[-C] [-B] [-P]\n");
        write_str(2, "       [--defined-only] [--no-sort] [--format=bsd|posix]");
        write_str(2, " file\n");
        sys_exit(1);
    }

    /* Set sort mode */
    if (opt_p) {
        sort_mode = 2; /* no sort */
    } else if (opt_n) {
        sort_mode = 1; /* sort by address */
    } else {
        sort_mode = 0; /* sort by name */
    }
    sort_reverse = opt_r;

    /* Read and parse ELF */
    elf_size = read_file(filename, file_buf, BUF_SIZE);
    elf_data = file_buf;
    parse_elf();

    if (opt_D) {
        list_symbols(opt_g, opt_u, opt_defined_only, opt_S, SHT_DYNSYM);
    } else {
        list_symbols(opt_g, opt_u, opt_defined_only, opt_S, SHT_SYMTAB);
    }

    sys_exit(0);
    return 0;
}
