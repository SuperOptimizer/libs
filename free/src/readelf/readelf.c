/*
 * readelf.c - ELF file inspector for the free toolchain
 * Usage: free-readelf [-h] [-S] [-s] [-r] [-l] [-d] [-n] [-e] [-a]
 *                     [--debug-dump=line] file
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
    write_str(2, "free-readelf: ");
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

static int str_len(const char *s)
{
    int n;

    n = 0;
    while (s[n]) {
        n++;
    }
    return n;
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

/* Print a hex value prefixed with 0x */
static void print_hex_prefix(u64 val, int width)
{
    write_str(1, "0x");
    print_hex(val, width);
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

/* Print right-aligned decimal in field of given width */
static void print_dec_right(u64 val, int width)
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
        write_str(2, "free-readelf: cannot open ");
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

/* ---- type name strings ---- */

static const char *elf_type_str(u16 type)
{
    switch (type) {
    case ET_NONE: return "NONE (No file type)";
    case ET_REL:  return "REL (Relocatable file)";
    case ET_EXEC: return "EXEC (Executable file)";
    case ET_DYN:  return "DYN (Shared object file)";
    default:      return "UNKNOWN";
    }
}

static const char *machine_str(u16 mach)
{
    switch (mach) {
    case EM_AARCH64: return "AArch64";
    case 62:         return "Advanced Micro Devices X86-64";
    case 3:          return "Intel 80386";
    case 40:         return "ARM";
    default:         return "Unknown";
    }
}

static const char *shtype_str(u32 type)
{
    switch (type) {
    case SHT_NULL:     return "NULL";
    case SHT_PROGBITS: return "PROGBITS";
    case SHT_SYMTAB:   return "SYMTAB";
    case SHT_STRTAB:   return "STRTAB";
    case SHT_RELA:     return "RELA";
    case SHT_HASH:     return "HASH";
    case SHT_DYNAMIC:  return "DYNAMIC";
    case SHT_NOTE:     return "NOTE";
    case SHT_NOBITS:   return "NOBITS";
    case SHT_DYNSYM:   return "DYNSYM";
    case 14:           return "INIT_ARRAY";
    case 15:           return "FINI_ARRAY";
    default:           return "UNKNOWN";
    }
}

static const char *phtype_str(u32 type)
{
    switch (type) {
    case PT_NULL:    return "NULL";
    case PT_LOAD:    return "LOAD";
    case PT_DYNAMIC: return "DYNAMIC";
    case PT_INTERP:  return "INTERP";
    case 4:          return "NOTE";
    case 6:          return "PHDR";
    case 7:          return "TLS";
    default:         return "UNKNOWN";
    }
}

static const char *sym_type_str(u8 info)
{
    switch (ELF64_ST_TYPE(info)) {
    case STT_NOTYPE:  return "NOTYPE";
    case STT_OBJECT:  return "OBJECT";
    case STT_FUNC:    return "FUNC";
    case STT_SECTION: return "SECTION";
    case STT_FILE:    return "FILE";
    default:          return "UNKNOWN";
    }
}

static const char *sym_bind_str(u8 info)
{
    switch (ELF64_ST_BIND(info)) {
    case STB_LOCAL:  return "LOCAL";
    case STB_GLOBAL: return "GLOBAL";
    case STB_WEAK:   return "WEAK";
    default:         return "UNKNOWN";
    }
}

static const char *sym_vis_str(u8 other)
{
    switch (ELF64_ST_VISIBILITY(other)) {
    case STV_DEFAULT: return "DEFAULT";
    case STV_HIDDEN:  return "HIDDEN";
    case 1:           return "INTERNAL";
    case 3:           return "PROTECTED";
    default:          return "UNKNOWN";
    }
}

static const char *reloc_type_str(u32 type)
{
    switch (type) {
    case R_AARCH64_NONE:              return "R_AARCH64_NONE";
    case R_AARCH64_ABS64:             return "R_AARCH64_ABS64";
    case R_AARCH64_ABS32:             return "R_AARCH64_ABS32";
    case R_AARCH64_PREL32:            return "R_AARCH64_PREL32";
    case R_AARCH64_CALL26:            return "R_AARCH64_CALL26";
    case R_AARCH64_JUMP26:            return "R_AARCH64_JUMP26";
    case R_AARCH64_ADR_PREL_PG_HI21: return "R_AARCH64_ADR_PREL_PG_HI21";
    case R_AARCH64_ADD_ABS_LO12_NC:  return "R_AARCH64_ADD_ABS_LO12_NC";
    case R_AARCH64_LDST8_ABS_LO12_NC:  return "R_AARCH64_LDST8_ABS_LO12_NC";
    case R_AARCH64_LDST16_ABS_LO12_NC: return "R_AARCH64_LDST16_ABS_LO12_NC";
    case R_AARCH64_LDST32_ABS_LO12_NC: return "R_AARCH64_LDST32_ABS_LO12_NC";
    case R_AARCH64_LDST64_ABS_LO12_NC: return "R_AARCH64_LDST64_ABS_LO12_NC";
    case R_AARCH64_LDST128_ABS_LO12_NC: return "R_AARCH64_LDST128_ABS_LO12_NC";
    case R_AARCH64_MOVW_UABS_G0_NC:  return "R_AARCH64_MOVW_UABS_G0_NC";
    case R_AARCH64_MOVW_UABS_G1_NC:  return "R_AARCH64_MOVW_UABS_G1_NC";
    case R_AARCH64_MOVW_UABS_G2_NC:  return "R_AARCH64_MOVW_UABS_G2_NC";
    case R_AARCH64_MOVW_UABS_G3:     return "R_AARCH64_MOVW_UABS_G3";
    case R_AARCH64_ADR_GOT_PAGE:      return "R_AARCH64_ADR_GOT_PAGE";
    case R_AARCH64_LD64_GOT_LO12_NC:  return "R_AARCH64_LD64_GOT_LO12_NC";
    case R_AARCH64_GLOB_DAT:          return "R_AARCH64_GLOB_DAT";
    case R_AARCH64_JUMP_SLOT:         return "R_AARCH64_JUMP_SLOT";
    case R_AARCH64_RELATIVE:          return "R_AARCH64_RELATIVE";
    default:                          return "UNKNOWN";
    }
}

static const char *dyn_tag_str(i64 tag)
{
    switch (tag) {
    case DT_NULL:      return "(NULL)";
    case DT_NEEDED:    return "(NEEDED)";
    case DT_PLTRELSZ:  return "(PLTRELSZ)";
    case DT_PLTGOT:    return "(PLTGOT)";
    case DT_HASH:      return "(HASH)";
    case DT_STRTAB:    return "(STRTAB)";
    case DT_SYMTAB:    return "(SYMTAB)";
    case DT_RELA:      return "(RELA)";
    case DT_RELASZ:    return "(RELASZ)";
    case DT_RELAENT:   return "(RELAENT)";
    case DT_STRSZ:     return "(STRSZ)";
    case DT_SYMENT:    return "(SYMENT)";
    case DT_SONAME:    return "(SONAME)";
    case DT_PLTREL:    return "(PLTREL)";
    case DT_JMPREL:    return "(JMPREL)";
    default:           return "(UNKNOWN)";
    }
}

/* ---- display ELF header ---- */

static void display_elf_header(void)
{
    int i;

    write_str(1, "ELF Header:\n");
    write_str(1, "  Magic:   ");
    for (i = 0; i < EI_NIDENT; i++) {
        print_hex(elf_ehdr->e_ident[i], 2);
        write_str(1, " ");
    }
    write_str(1, "\n");

    write_str(1, "  Class:                             ");
    write_str(1, elf_ehdr->e_ident[4] == ELFCLASS64 ? "ELF64" : "ELF32");
    write_str(1, "\n");

    write_str(1, "  Data:                              ");
    write_str(1, elf_ehdr->e_ident[5] == ELFDATA2LSB ?
              "2's complement, little endian" : "2's complement, big endian");
    write_str(1, "\n");

    write_str(1, "  Version:                           ");
    print_dec((u64)elf_ehdr->e_ident[6]);
    write_str(1, " (current)\n");

    write_str(1, "  OS/ABI:                            UNIX - System V\n");

    write_str(1, "  Type:                              ");
    write_str(1, elf_type_str(elf_ehdr->e_type));
    write_str(1, "\n");

    write_str(1, "  Machine:                           ");
    write_str(1, machine_str(elf_ehdr->e_machine));
    write_str(1, "\n");

    write_str(1, "  Version:                           ");
    print_hex_prefix(elf_ehdr->e_version, 1);
    write_str(1, "\n");

    write_str(1, "  Entry point address:               ");
    print_hex_prefix(elf_ehdr->e_entry, 1);
    write_str(1, "\n");

    write_str(1, "  Start of program headers:          ");
    print_dec(elf_ehdr->e_phoff);
    write_str(1, " (bytes into file)\n");

    write_str(1, "  Start of section headers:          ");
    print_dec(elf_ehdr->e_shoff);
    write_str(1, " (bytes into file)\n");

    write_str(1, "  Flags:                             ");
    print_hex_prefix(elf_ehdr->e_flags, 1);
    write_str(1, "\n");

    write_str(1, "  Size of this header:               ");
    print_dec((u64)elf_ehdr->e_ehsize);
    write_str(1, " (bytes)\n");

    write_str(1, "  Size of program headers:           ");
    print_dec((u64)elf_ehdr->e_phentsize);
    write_str(1, " (bytes)\n");

    write_str(1, "  Number of program headers:         ");
    print_dec((u64)elf_ehdr->e_phnum);
    write_str(1, "\n");

    write_str(1, "  Size of section headers:           ");
    print_dec((u64)elf_ehdr->e_shentsize);
    write_str(1, " (bytes)\n");

    write_str(1, "  Number of section headers:         ");
    print_dec((u64)elf_ehdr->e_shnum);
    write_str(1, "\n");

    write_str(1, "  Section header string table index: ");
    print_dec((u64)elf_ehdr->e_shstrndx);
    write_str(1, "\n");
}

/* ---- display section headers ---- */

static void display_section_headers(void)
{
    int i;
    const Elf64_Shdr *sh;
    char flags_str[16];
    int fi;

    write_str(1, "There are ");
    print_dec((u64)elf_ehdr->e_shnum);
    write_str(1, " section headers, starting at offset ");
    print_hex_prefix(elf_ehdr->e_shoff, 1);
    write_str(1, ":\n\n");

    write_str(1, "Section Headers:\n");
    write_str(1, "  [Nr] Name              Type            ");
    write_str(1, "Address          Off    Size   ES Flg Lk Inf Al\n");

    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];

        /* [Nr] */
        write_str(1, "  [");
        if (i < 10) {
            write_str(1, " ");
        }
        print_dec((u64)i);
        write_str(1, "] ");

        /* Name */
        print_padded(section_name(i), 18);

        /* Type */
        print_padded(shtype_str(sh->sh_type), 16);

        /* Address */
        print_hex(sh->sh_addr, 16);
        write_str(1, " ");

        /* Offset */
        print_hex(sh->sh_offset, 6);
        write_str(1, " ");

        /* Size */
        print_hex(sh->sh_size, 6);
        write_str(1, " ");

        /* Entry size */
        print_hex(sh->sh_entsize, 2);
        write_str(1, " ");

        /* Flags */
        fi = 0;
        if (sh->sh_flags & SHF_WRITE)     { flags_str[fi++] = 'W'; }
        if (sh->sh_flags & SHF_ALLOC)     { flags_str[fi++] = 'A'; }
        if (sh->sh_flags & SHF_EXECINSTR) { flags_str[fi++] = 'X'; }
        if (sh->sh_flags & 0x10)          { flags_str[fi++] = 'M'; }
        if (sh->sh_flags & 0x20)          { flags_str[fi++] = 'S'; }
        if (sh->sh_flags & SHF_INFO_LINK) { flags_str[fi++] = 'I'; }
        flags_str[fi] = '\0';
        print_padded(flags_str, 4);

        /* Link */
        print_dec_right((u64)sh->sh_link, 2);
        write_str(1, " ");

        /* Info */
        print_dec_right((u64)sh->sh_info, 3);
        write_str(1, " ");

        /* Align */
        print_dec_right(sh->sh_addralign, 2);
        write_str(1, "\n");
    }

    write_str(1, "Key to Flags:\n");
    write_str(1, "  W (write), A (alloc), X (execute), M (merge), ");
    write_str(1, "S (strings), I (info)\n");
}

/* ---- display symbol tables ---- */

static void display_symtab(u32 sh_type)
{
    int i;
    int j;
    const Elf64_Shdr *sh;
    const Elf64_Sym *sym;
    const char *strtab;
    int nsyms;
    const char *name;

    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];
        if (sh->sh_type != sh_type) {
            continue;
        }

        nsyms = (int)(sh->sh_size / sh->sh_entsize);
        strtab = elf_data + elf_shdrs[sh->sh_link].sh_offset;

        write_str(1, "\nSymbol table '");
        write_str(1, section_name(i));
        write_str(1, "' contains ");
        print_dec((u64)nsyms);
        write_str(1, " entries:\n");
        write_str(1, "   Num:    Value          Size Type    Bind");
        write_str(1, "   Vis      Ndx Name\n");

        for (j = 0; j < nsyms; j++) {
            sym = (const Elf64_Sym *)(elf_data + sh->sh_offset) + j;

            /* Num */
            print_dec_right((u64)j, 6);
            write_str(1, ": ");

            /* Value */
            print_hex(sym->st_value, 16);
            write_str(1, " ");

            /* Size */
            print_dec_right(sym->st_size, 5);
            write_str(1, " ");

            /* Type */
            print_padded(sym_type_str(sym->st_info), 8);

            /* Bind */
            print_padded(sym_bind_str(sym->st_info), 7);

            /* Vis */
            print_padded(sym_vis_str(sym->st_other), 8);

            /* Ndx */
            if (sym->st_shndx == SHN_UNDEF) {
                write_str(1, "UND ");
            } else if (sym->st_shndx == SHN_ABS) {
                write_str(1, "ABS ");
            } else if (sym->st_shndx == SHN_COMMON) {
                write_str(1, "COM ");
            } else {
                print_dec_right((u64)sym->st_shndx, 3);
                write_str(1, " ");
            }

            /* Name */
            name = strtab + sym->st_name;
            if (name[0] == '\0' &&
                ELF64_ST_TYPE(sym->st_info) == STT_SECTION) {
                if (sym->st_shndx < elf_ehdr->e_shnum) {
                    name = section_name(sym->st_shndx);
                }
            }
            write_str(1, name);
            write_str(1, "\n");
        }
    }
}

static void display_symbols(void)
{
    display_symtab(SHT_SYMTAB);
    display_symtab(SHT_DYNSYM);
}

/* ---- display relocations ---- */

static void display_relocations(void)
{
    int i;
    int j;
    const Elf64_Shdr *sh;
    const Elf64_Rela *rela;
    const Elf64_Shdr *symtab_sh;
    const Elf64_Sym *sym;
    const char *strtab;
    int nrels;
    u32 sym_idx;
    u32 rel_type;
    const char *sname;

    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];
        if (sh->sh_type != SHT_RELA) {
            continue;
        }

        nrels = (int)(sh->sh_size / sh->sh_entsize);
        write_str(1, "\nRelocation section '");
        write_str(1, section_name(i));
        write_str(1, "' at offset ");
        print_hex_prefix(sh->sh_offset, 1);
        write_str(1, " contains ");
        print_dec((u64)nrels);
        write_str(1, " entries:\n");
        write_str(1, "  Offset          Info           Type");
        write_str(1, "                         Sym. Value    Sym. Name + Addend\n");

        symtab_sh = &elf_shdrs[sh->sh_link];
        strtab = elf_data + elf_shdrs[symtab_sh->sh_link].sh_offset;

        for (j = 0; j < nrels; j++) {
            rela = (const Elf64_Rela *)(elf_data + sh->sh_offset) + j;
            sym_idx = (u32)ELF64_R_SYM(rela->r_info);
            rel_type = ELF64_R_TYPE(rela->r_info);
            sym = (const Elf64_Sym *)(elf_data + symtab_sh->sh_offset) +
                  sym_idx;

            print_hex(rela->r_offset, 12);
            write_str(1, "  ");
            print_hex(rela->r_info, 12);
            write_str(1, " ");
            print_padded(reloc_type_str(rel_type), 30);

            print_hex(sym->st_value, 16);
            write_str(1, " ");

            sname = strtab + sym->st_name;
            if (sname[0] == '\0' &&
                ELF64_ST_TYPE(sym->st_info) == STT_SECTION) {
                if (sym->st_shndx < elf_ehdr->e_shnum) {
                    sname = section_name(sym->st_shndx);
                }
            }
            write_str(1, sname);

            if (rela->r_addend > 0) {
                write_str(1, " + ");
                print_hex((u64)rela->r_addend, 1);
            } else if (rela->r_addend < 0) {
                write_str(1, " - ");
                print_hex((u64)(-rela->r_addend), 1);
            }
            write_str(1, "\n");
        }
    }
}

/* ---- display program headers ---- */

static void display_program_headers(void)
{
    int i;
    const Elf64_Phdr *ph;
    char flags_str[4];
    int fi;

    if (elf_phdrs == NULL || elf_ehdr->e_phnum == 0) {
        write_str(1, "\nThere are no program headers in this file.\n");
        return;
    }

    write_str(1, "\nElf file type is ");
    write_str(1, elf_type_str(elf_ehdr->e_type));
    write_str(1, "\nEntry point ");
    print_hex_prefix(elf_ehdr->e_entry, 1);
    write_str(1, "\nThere are ");
    print_dec((u64)elf_ehdr->e_phnum);
    write_str(1, " program headers, starting at offset ");
    print_dec(elf_ehdr->e_phoff);
    write_str(1, "\n\n");

    write_str(1, "Program Headers:\n");
    write_str(1, "  Type           Offset   VirtAddr         ");
    write_str(1, "PhysAddr         FileSiz  MemSiz   Flg Align\n");

    for (i = 0; i < elf_ehdr->e_phnum; i++) {
        ph = &elf_phdrs[i];

        write_str(1, "  ");
        print_padded(phtype_str(ph->p_type), 15);

        print_hex_prefix(ph->p_offset, 6);
        write_str(1, " ");
        print_hex_prefix(ph->p_vaddr, 16);
        write_str(1, " ");
        print_hex_prefix(ph->p_paddr, 16);
        write_str(1, " ");
        print_hex_prefix(ph->p_filesz, 6);
        write_str(1, " ");
        print_hex_prefix(ph->p_memsz, 6);
        write_str(1, " ");

        fi = 0;
        if (ph->p_flags & PF_R) { flags_str[fi++] = 'R'; }
        if (ph->p_flags & PF_W) { flags_str[fi++] = 'W'; }
        if (ph->p_flags & PF_X) { flags_str[fi++] = 'E'; }
        flags_str[fi] = '\0';
        print_padded(flags_str, 4);

        print_hex_prefix(ph->p_align, 1);
        write_str(1, "\n");
    }
}

/* ---- display dynamic section ---- */

static void display_dynamic(void)
{
    int i;
    int j;
    const Elf64_Shdr *sh;
    const Elf64_Dyn *dyn;
    int nentries;
    const char *dynstrtab;

    /* Find dynamic string table */
    dynstrtab = NULL;
    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        if (elf_shdrs[i].sh_type == SHT_STRTAB &&
            streq(section_name(i), ".dynstr")) {
            dynstrtab = elf_data + elf_shdrs[i].sh_offset;
            break;
        }
    }

    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];
        if (sh->sh_type != SHT_DYNAMIC) {
            continue;
        }

        nentries = (int)(sh->sh_size / sh->sh_entsize);
        write_str(1, "\nDynamic section at offset ");
        print_hex_prefix(sh->sh_offset, 1);
        write_str(1, " contains ");
        print_dec((u64)nentries);
        write_str(1, " entries:\n");
        write_str(1, "  Tag        Type                         ");
        write_str(1, "Name/Value\n");

        for (j = 0; j < nentries; j++) {
            dyn = (const Elf64_Dyn *)(elf_data + sh->sh_offset) + j;

            write_str(1, " ");
            print_hex_prefix((u64)dyn->d_tag, 16);
            write_str(1, " ");
            print_padded(dyn_tag_str(dyn->d_tag), 21);

            if (dyn->d_tag == DT_NEEDED && dynstrtab != NULL) {
                write_str(1, "Shared library: [");
                write_str(1, dynstrtab + dyn->d_un.d_val);
                write_str(1, "]");
            } else if (dyn->d_tag == DT_SONAME && dynstrtab != NULL) {
                write_str(1, "Library soname: [");
                write_str(1, dynstrtab + dyn->d_un.d_val);
                write_str(1, "]");
            } else {
                print_hex_prefix(dyn->d_un.d_val, 1);
            }
            write_str(1, "\n");

            if (dyn->d_tag == DT_NULL) {
                break;
            }
        }
    }
}

/* ---- display notes ---- */

static void display_notes(void)
{
    int i;
    const Elf64_Shdr *sh;
    const u8 *data;
    u32 namesz;
    u32 descsz;
    u32 note_type;
    long pos;
    long end;

    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];
        if (sh->sh_type != SHT_NOTE) {
            continue;
        }

        write_str(1, "\nDisplaying notes found in: ");
        write_str(1, section_name(i));
        write_str(1, "\n");
        write_str(1, "  Owner                Data size\tDescription\n");

        data = (const u8 *)(elf_data + sh->sh_offset);
        pos = 0;
        end = (long)sh->sh_size;

        while (pos + 12 <= end) {
            namesz = *(const u32 *)(data + pos);
            descsz = *(const u32 *)(data + pos + 4);
            note_type = *(const u32 *)(data + pos + 8);
            pos += 12;

            write_str(1, "  ");
            if (namesz > 0 && pos + (long)namesz <= end) {
                write_str(1, (const char *)(data + pos));
            }
            /* Align namesz to 4 */
            pos += (long)((namesz + 3) & ~3u);

            write_str(1, "\t\t");
            print_hex_prefix((u64)descsz, 1);
            write_str(1, "\t\ttype: ");
            print_hex_prefix((u64)note_type, 1);
            write_str(1, "\n");

            /* Align descsz to 4 */
            pos += (long)((descsz + 3) & ~3u);
        }
    }
}

/* ---- display debug_line ---- */

static void display_debug_line(void)
{
    int i;
    const Elf64_Shdr *sh;

    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];
        if (sh->sh_type == SHT_PROGBITS &&
            streq(section_name(i), ".debug_line")) {
            write_str(1, "\nRaw dump of debug contents of section ");
            write_str(1, ".debug_line:\n\n");
            write_str(1, "  Offset: ");
            print_hex_prefix(sh->sh_offset, 1);
            write_str(1, "\n  Size:   ");
            print_hex_prefix(sh->sh_size, 1);
            write_str(1, "\n\n");

            /* Dump raw hex like readelf --debug-dump=line */
            {
                const u8 *data;
                long j;
                long size;

                data = (const u8 *)(elf_data + sh->sh_offset);
                size = (long)sh->sh_size;

                for (j = 0; j < size; j += 16) {
                    long k;
                    long line_len;

                    write_str(1, "  ");
                    print_hex((u64)j, 8);
                    write_str(1, " ");

                    line_len = size - j;
                    if (line_len > 16) {
                        line_len = 16;
                    }
                    for (k = 0; k < line_len; k++) {
                        print_hex(data[j + k], 2);
                        write_str(1, " ");
                    }
                    write_str(1, "\n");
                }
            }
            return;
        }
    }
    write_str(1, "\nNo .debug_line section found.\n");
}

/* ---- entry point ---- */
int main(int argc, char **argv)
{
    int opt_h;
    int opt_S;
    int opt_s;
    int opt_r;
    int opt_l;
    int opt_d;
    int opt_n;
    int opt_debug_line;
    const char *filename;
    int i;
    const char *arg;

    opt_h = 0;
    opt_S = 0;
    opt_s = 0;
    opt_r = 0;
    opt_l = 0;
    opt_d = 0;
    opt_n = 0;
    opt_debug_line = 0;
    filename = NULL;

    /* Handle --version early */
    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "--version")) {
            write_str(1, "GNU readelf (free-readelf) 2.42\n");
            sys_exit(0);
        }
    }

    for (i = 1; i < argc; i++) {
        arg = argv[i];
        if (streq(arg, "-h") || streq(arg, "--file-header")) {
            opt_h = 1;
        } else if (streq(arg, "-S") || streq(arg, "--section-headers") ||
                   streq(arg, "--sections")) {
            opt_S = 1;
        } else if (streq(arg, "-s") || streq(arg, "--syms") ||
                   streq(arg, "--symbols")) {
            opt_s = 1;
        } else if (streq(arg, "-r") || streq(arg, "--relocs")) {
            opt_r = 1;
        } else if (streq(arg, "-l") || streq(arg, "--program-headers") ||
                   streq(arg, "--segments")) {
            opt_l = 1;
        } else if (streq(arg, "-d") || streq(arg, "--dynamic")) {
            opt_d = 1;
        } else if (streq(arg, "-n") || streq(arg, "--notes")) {
            opt_n = 1;
        } else if (streq(arg, "-e") || streq(arg, "--headers")) {
            opt_h = 1;
            opt_l = 1;
            opt_S = 1;
        } else if (streq(arg, "-a") || streq(arg, "--all")) {
            opt_h = 1;
            opt_l = 1;
            opt_S = 1;
            opt_s = 1;
            opt_r = 1;
            opt_d = 1;
            opt_n = 1;
        } else if (streq(arg, "--debug-dump=line")) {
            opt_debug_line = 1;
        } else if (str_startswith(arg, "--debug-dump=")) {
            /* Accept but ignore other debug dump types for now */
        } else if (arg[0] == '-') {
            write_str(2, "free-readelf: unknown option '");
            write_str(2, arg);
            write_str(2, "'\n");
            sys_exit(1);
        } else {
            filename = arg;
        }
    }

    if (filename == NULL) {
        write_str(2, "Usage: free-readelf [-h] [-S] [-s] [-r] [-l] [-d] ");
        write_str(2, "[-n] [-e] [-a] [--debug-dump=line] file\n");
        sys_exit(1);
    }

    /* Read and parse ELF */
    elf_size = read_file(filename, file_buf, BUF_SIZE);
    elf_data = file_buf;
    parse_elf();

    if (opt_h) {
        display_elf_header();
    }
    if (opt_S) {
        display_section_headers();
    }
    if (opt_s) {
        display_symbols();
    }
    if (opt_r) {
        display_relocations();
    }
    if (opt_l) {
        display_program_headers();
    }
    if (opt_d) {
        display_dynamic();
    }
    if (opt_n) {
        display_notes();
    }
    if (opt_debug_line) {
        display_debug_line();
    }

    sys_exit(0);
    return 0;
}
