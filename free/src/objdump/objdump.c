/*
 * objdump.c - ELF inspector/disassembler for the free toolchain
 * Usage: free-objdump [-d] [-t] [-h] [-r] [-s] file
 * Pure C89, freestanding with OS syscalls
 */

#include "../../include/free.h"
#include "../../include/elf.h"
#include "../../include/aarch64.h"

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
#define BUF_SIZE    (16 * 1024 * 1024)

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
    write_str(2, "free-objdump: ");
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

/* Print signed decimal i64 */
static void print_sdec(i64 val)
{
    if (val < 0) {
        write_str(1, "-");
        print_dec((u64)(-val));
    } else {
        print_dec((u64)val);
    }
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
        write_str(2, "free-objdump: cannot open ");
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

/* ---- ELF parsing helpers ---- */

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
    return elf_shstrtab + elf_shdrs[idx].sh_name;
}

static const char *shtype_str(u32 type)
{
    switch (type) {
    case SHT_NULL:     return "NULL";
    case SHT_PROGBITS: return "PROGBITS";
    case SHT_SYMTAB:   return "SYMTAB";
    case SHT_STRTAB:   return "STRTAB";
    case SHT_RELA:     return "RELA";
    case SHT_NOBITS:   return "NOBITS";
    default:           return "UNKNOWN";
    }
}

static const char *sym_type_str(u8 info)
{
    switch (ELF64_ST_TYPE(info)) {
    case STT_NOTYPE:  return "NOTYPE";
    case STT_FUNC:    return "FUNC";
    case STT_SECTION: return "SECTION";
    default:          return "OTHER";
    }
}

static const char *sym_bind_str(u8 info)
{
    switch (ELF64_ST_BIND(info)) {
    case STB_LOCAL:  return "LOCAL";
    case STB_GLOBAL: return "GLOBAL";
    default:         return "OTHER";
    }
}

static const char *reloc_type_str(u32 type)
{
    switch (type) {
    case R_AARCH64_NONE:              return "R_AARCH64_NONE";
    case R_AARCH64_ABS64:             return "R_AARCH64_ABS64";
    case R_AARCH64_ABS32:             return "R_AARCH64_ABS32";
    case R_AARCH64_CALL26:            return "R_AARCH64_CALL26";
    case R_AARCH64_JUMP26:            return "R_AARCH64_JUMP26";
    case R_AARCH64_ADR_PREL_PG_HI21: return "R_AARCH64_ADR_PREL_PG_HI21";
    case R_AARCH64_ADD_ABS_LO12_NC:  return "R_AARCH64_ADD_ABS_LO12_NC";
    case R_AARCH64_LDST8_ABS_LO12_NC:  return "R_AARCH64_LDST8_ABS_LO12_NC";
    case R_AARCH64_LDST64_ABS_LO12_NC: return "R_AARCH64_LDST64_ABS_LO12_NC";
    case R_AARCH64_MOVW_UABS_G0_NC:  return "R_AARCH64_MOVW_UABS_G0_NC";
    case R_AARCH64_MOVW_UABS_G1_NC:  return "R_AARCH64_MOVW_UABS_G1_NC";
    case R_AARCH64_MOVW_UABS_G2_NC:  return "R_AARCH64_MOVW_UABS_G2_NC";
    case R_AARCH64_MOVW_UABS_G3:     return "R_AARCH64_MOVW_UABS_G3";
    default:                          return "UNKNOWN";
    }
}

/* ---- display section headers ---- */
static void display_headers(void)
{
    int i;
    const Elf64_Shdr *sh;
    char flags_str[8];
    int fi;

    write_str(1, "\nSections:\n");
    write_str(1, "Idx Name              Type      Flags  ");
    write_str(1, "Addr             Offset   Size     Align\n");

    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];

        /* Index */
        if (i < 10) {
            write_str(1, "  ");
        } else {
            write_str(1, " ");
        }
        print_dec((u64)i);
        write_str(1, " ");

        /* Name */
        print_padded(section_name(i), 18);

        /* Type */
        print_padded(shtype_str(sh->sh_type), 10);

        /* Flags */
        fi = 0;
        if (sh->sh_flags & SHF_WRITE)     { flags_str[fi++] = 'W'; }
        if (sh->sh_flags & SHF_ALLOC)     { flags_str[fi++] = 'A'; }
        if (sh->sh_flags & SHF_EXECINSTR) { flags_str[fi++] = 'X'; }
        flags_str[fi] = '\0';
        print_padded(flags_str, 7);

        /* Address */
        print_hex(sh->sh_addr, 16);
        write_str(1, " ");

        /* Offset */
        print_hex(sh->sh_offset, 8);
        write_str(1, " ");

        /* Size */
        print_hex(sh->sh_size, 8);
        write_str(1, " ");

        /* Align */
        print_dec(sh->sh_addralign);
        write_str(1, "\n");
    }
}

/* ---- display symbol table ---- */
static void display_symbols(void)
{
    int i;
    int j;
    const Elf64_Shdr *sh;
    const Elf64_Sym *sym;
    const char *strtab;
    int nsyms;
    const char *name;

    write_str(1, "\nSymbol table:\n");
    write_str(1, "  Num  Value            Size  Type    Bind    Name\n");

    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];
        if (sh->sh_type != SHT_SYMTAB) {
            continue;
        }

        strtab = elf_data + elf_shdrs[sh->sh_link].sh_offset;
        nsyms = (int)(sh->sh_size / sh->sh_entsize);

        for (j = 0; j < nsyms; j++) {
            sym = (const Elf64_Sym *)(elf_data + sh->sh_offset) + j;

            /* Num */
            if (j < 10) {
                write_str(1, "    ");
            } else if (j < 100) {
                write_str(1, "   ");
            } else {
                write_str(1, "  ");
            }
            print_dec((u64)j);
            write_str(1, "  ");

            /* Value */
            print_hex(sym->st_value, 16);
            write_str(1, " ");

            /* Size */
            if (sym->st_size < 10) {
                write_str(1, "    ");
            } else if (sym->st_size < 100) {
                write_str(1, "   ");
            } else if (sym->st_size < 1000) {
                write_str(1, "  ");
            } else {
                write_str(1, " ");
            }
            print_dec(sym->st_size);
            write_str(1, "  ");

            /* Type */
            print_padded(sym_type_str(sym->st_info), 8);

            /* Binding */
            print_padded(sym_bind_str(sym->st_info), 8);

            /* Name */
            name = strtab + sym->st_name;
            if (name[0] == '\0' && ELF64_ST_TYPE(sym->st_info) == STT_SECTION) {
                name = section_name(sym->st_shndx);
            }
            write_str(1, name);
            write_str(1, "\n");
        }
    }
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

    for (i = 0; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];
        if (sh->sh_type != SHT_RELA) {
            continue;
        }

        write_str(1, "\nRelocation section '");
        write_str(1, section_name(i));
        write_str(1, "':\n");
        write_str(1, "  Offset           Type                     ");
        write_str(1, "Symbol               Addend\n");

        symtab_sh = &elf_shdrs[sh->sh_link];
        strtab = elf_data + elf_shdrs[symtab_sh->sh_link].sh_offset;
        nrels = (int)(sh->sh_size / sh->sh_entsize);

        for (j = 0; j < nrels; j++) {
            rela = (const Elf64_Rela *)(elf_data + sh->sh_offset) + j;
            sym_idx = (u32)ELF64_R_SYM(rela->r_info);
            rel_type = ELF64_R_TYPE(rela->r_info);

            sym = (const Elf64_Sym *)(elf_data + symtab_sh->sh_offset) + sym_idx;

            /* Offset */
            write_str(1, "  ");
            print_hex(rela->r_offset, 16);
            write_str(1, " ");

            /* Type */
            print_padded(reloc_type_str(rel_type), 25);

            /* Symbol */
            {
                const char *sname;
                sname = strtab + sym->st_name;
                if (sname[0] == '\0' &&
                    ELF64_ST_TYPE(sym->st_info) == STT_SECTION) {
                    sname = section_name(sym->st_shndx);
                }
                print_padded(sname, 21);
            }

            /* Addend */
            if (rela->r_addend >= 0) {
                write_str(1, "+");
                print_hex((u64)rela->r_addend, 1);
            } else {
                write_str(1, "-");
                print_hex((u64)(-rela->r_addend), 1);
            }
            write_str(1, "\n");
        }
    }
}

/* ---- display section contents as hex dump ---- */
static void display_contents(void)
{
    int i;
    int j;
    int k;
    const Elf64_Shdr *sh;
    const u8 *data;
    u64 addr;
    int remaining;
    int line_len;

    for (i = 1; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];
        if (sh->sh_type == SHT_NOBITS || sh->sh_size == 0) {
            continue;
        }

        write_str(1, "\nContents of section ");
        write_str(1, section_name(i));
        write_str(1, ":\n");

        data = (const u8 *)(elf_data + sh->sh_offset);
        addr = sh->sh_addr;

        for (j = 0; j < (int)sh->sh_size; j += 16) {
            remaining = (int)sh->sh_size - j;
            line_len = (remaining < 16) ? remaining : 16;

            /* Address */
            write_str(1, " ");
            print_hex(addr + (u64)j, 8);
            write_str(1, " ");

            /* Hex bytes in groups of 4 */
            for (k = 0; k < 16; k++) {
                if (k > 0 && (k % 4) == 0) {
                    write_str(1, " ");
                }
                if (k < line_len) {
                    print_hex(data[j + k], 2);
                } else {
                    write_str(1, "  ");
                }
            }

            write_str(1, "  ");

            /* ASCII */
            for (k = 0; k < line_len; k++) {
                u8 c;
                char ch[2];
                c = data[j + k];
                if (c >= 0x20 && c < 0x7f) {
                    ch[0] = (char)c;
                } else {
                    ch[0] = '.';
                }
                ch[1] = '\0';
                write_str(1, ch);
            }
            write_str(1, "\n");
        }
    }
}

/* ---- aarch64 disassembler ---- */

static const char *reg_name_x(int r)
{
    static const char *names[] = {
        "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",
        "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "fp",  "lr",  "sp"
    };
    if (r < 0 || r > 31) {
        return "?";
    }
    return names[r];
}

static const char *reg_name_w(int r)
{
    static const char *names[] = {
        "w0",  "w1",  "w2",  "w3",  "w4",  "w5",  "w6",  "w7",
        "w8",  "w9",  "w10", "w11", "w12", "w13", "w14", "w15",
        "w16", "w17", "w18", "w19", "w20", "w21", "w22", "w23",
        "w24", "w25", "w26", "w27", "w28", "w29", "w30", "wzr"
    };
    if (r < 0 || r > 31) {
        return "?";
    }
    return names[r];
}

/* XZR when used as destination in certain instructions */
static const char *reg_name_xzr(int r)
{
    if (r == 31) {
        return "xzr";
    }
    return reg_name_x(r);
}

static const char *cond_name(int cond)
{
    static const char *names[] = {
        "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
        "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"
    };
    if (cond < 0 || cond > 15) {
        return "?";
    }
    return names[cond];
}

/* Sign-extend a value from bit_width bits */
static i64 sign_extend(u64 val, int bit_width)
{
    u64 sign_bit;

    sign_bit = (u64)1 << (bit_width - 1);
    if (val & sign_bit) {
        val |= ~(((u64)1 << bit_width) - 1);
    }
    return (i64)val;
}

/* Disassemble one aarch64 instruction, write mnemonic to stdout */
/* Returns 1 if decoded, 0 if unknown */
static int disasm_insn(u32 insn, u64 addr)
{
    int rd;
    int rn;
    int rm;
    int ra;
    int rt;
    int rt2;
    u32 imm;
    i64 offset;
    int sf;
    int opc;
    int shift;

    rd = insn & 0x1f;
    rn = (insn >> 5) & 0x1f;
    rm = (insn >> 16) & 0x1f;

    /* NOP: 0xD503201F */
    if (insn == 0xD503201F) {
        write_str(1, "nop");
        return 1;
    }

    /* RET: 0xD65F03C0 (RET X30), or D65F0000 | (rn << 5) */
    if ((insn & 0xFFFFFC1F) == 0xD65F0000) {
        rn = (insn >> 5) & 0x1f;
        if (rn == 30) {
            write_str(1, "ret");
        } else {
            write_str(1, "ret\t");
            write_str(1, reg_name_x(rn));
        }
        return 1;
    }

    /* SVC */
    if ((insn & 0xFFE0001F) == 0xD4000001) {
        imm = (insn >> 5) & 0xffff;
        write_str(1, "svc\t#0x");
        print_hex(imm, 4);
        return 1;
    }

    /* BR Xn */
    if ((insn & 0xFFFFFC1F) == 0xD61F0000) {
        rn = (insn >> 5) & 0x1f;
        write_str(1, "br\t");
        write_str(1, reg_name_x(rn));
        return 1;
    }

    /* BLR Xn */
    if ((insn & 0xFFFFFC1F) == 0xD63F0000) {
        rn = (insn >> 5) & 0x1f;
        write_str(1, "blr\t");
        write_str(1, reg_name_x(rn));
        return 1;
    }

    /* B imm26 */
    if ((insn & 0xFC000000) == 0x14000000) {
        offset = sign_extend(insn & 0x03FFFFFF, 26) * 4;
        write_str(1, "b\t");
        print_hex((u64)((i64)addr + offset), 1);
        return 1;
    }

    /* BL imm26 */
    if ((insn & 0xFC000000) == 0x94000000) {
        offset = sign_extend(insn & 0x03FFFFFF, 26) * 4;
        write_str(1, "bl\t");
        print_hex((u64)((i64)addr + offset), 1);
        return 1;
    }

    /* B.cond imm19 */
    if ((insn & 0xFF000010) == 0x54000000) {
        int cond;
        cond = insn & 0xf;
        offset = sign_extend((insn >> 5) & 0x7FFFF, 19) * 4;
        write_str(1, "b.");
        write_str(1, cond_name(cond));
        write_str(1, "\t");
        print_hex((u64)((i64)addr + offset), 1);
        return 1;
    }

    /* CBZ/CBNZ */
    if ((insn & 0x7E000000) == 0x34000000) {
        sf = (insn >> 31) & 1;
        opc = (insn >> 24) & 1;
        rt = insn & 0x1f;
        offset = sign_extend((insn >> 5) & 0x7FFFF, 19) * 4;
        write_str(1, opc ? "cbnz" : "cbz");
        write_str(1, "\t");
        write_str(1, sf ? reg_name_x(rt) : reg_name_w(rt));
        write_str(1, ", ");
        print_hex((u64)((i64)addr + offset), 1);
        return 1;
    }

    /* MOVZ / MOVN / MOVK (64-bit) */
    if ((insn & 0x1F800000) == 0x00800000 &&
        ((insn >> 29) & 0x7) >= 4 && ((insn >> 29) & 0x7) <= 6) {
        opc = (insn >> 29) & 0x7;
        sf = (insn >> 31) & 1;
        shift = ((insn >> 21) & 0x3) * 16;
        imm = (insn >> 5) & 0xffff;
        rd = insn & 0x1f;
        if (opc == 5) {
            write_str(1, "movz\t");
        } else if (opc == 7) {
            write_str(1, "movk\t");
        } else if (opc == 4) {
            write_str(1, "movn\t");
        } else {
            write_str(1, ".inst\t0x");
            print_hex(insn, 8);
            return 1;
        }
        write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
        write_str(1, ", #0x");
        print_hex(imm, 4);
        if (shift > 0) {
            write_str(1, ", lsl #");
            print_dec((u64)shift);
        }
        return 1;
    }

    /* ADD/SUB immediate */
    if ((insn & 0x1F000000) == 0x11000000) {
        sf = (insn >> 31) & 1;
        opc = (insn >> 29) & 0x3;
        imm = (insn >> 10) & 0xfff;
        shift = ((insn >> 22) & 0x1) ? 12 : 0;
        rn = (insn >> 5) & 0x1f;
        rd = insn & 0x1f;

        /* CMP is SUBS with rd=XZR/WZR */
        if ((opc & 0x3) == 0x3 && rd == 31) {
            write_str(1, "cmp\t");
            write_str(1, sf ? reg_name_x(rn) : reg_name_w(rn));
            write_str(1, ", #0x");
            print_hex(imm, 1);
            if (shift == 12) {
                write_str(1, ", lsl #12");
            }
            return 1;
        }

        switch (opc) {
        case 0: write_str(1, "add\t"); break;
        case 1: write_str(1, "adds\t"); break;
        case 2: write_str(1, "sub\t"); break;
        case 3: write_str(1, "subs\t"); break;
        }

        /* rd might be SP for add/sub */
        if ((opc & 1) == 0 && rd == 31) {
            write_str(1, sf ? "sp" : "wsp");
        } else {
            write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
        }
        write_str(1, ", ");
        if (rn == 31 && (opc & 1) == 0) {
            write_str(1, sf ? "sp" : "wsp");
        } else {
            write_str(1, sf ? reg_name_x(rn) : reg_name_w(rn));
        }
        write_str(1, ", #0x");
        print_hex(imm, 1);
        if (shift == 12) {
            write_str(1, ", lsl #12");
        }
        return 1;
    }

    /* ADD/SUB shifted register */
    if ((insn & 0x1F200000) == 0x0B000000) {
        sf = (insn >> 31) & 1;
        opc = (insn >> 29) & 0x3;
        rm = (insn >> 16) & 0x1f;
        rn = (insn >> 5) & 0x1f;
        rd = insn & 0x1f;

        /* CMP is SUBS with rd=XZR */
        if ((opc & 0x3) == 0x3 && rd == 31) {
            write_str(1, "cmp\t");
            write_str(1, sf ? reg_name_x(rn) : reg_name_w(rn));
            write_str(1, ", ");
            write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
            return 1;
        }

        /* NEG is SUB from XZR */
        if ((opc & 0x2) && rn == 31) {
            write_str(1, (opc & 1) ? "negs\t" : "neg\t");
            write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
            write_str(1, ", ");
            write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
            return 1;
        }

        /* MOV is ORR with rn=XZR - but this is ADD/SUB section */
        switch (opc) {
        case 0: write_str(1, "add\t"); break;
        case 1: write_str(1, "adds\t"); break;
        case 2: write_str(1, "sub\t"); break;
        case 3: write_str(1, "subs\t"); break;
        }
        write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_x(rn) : reg_name_w(rn));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
        return 1;
    }

    /* Logical shifted register (AND, ORR, EOR, ANDS) */
    if ((insn & 0x1F200000) == 0x0A000000) {
        sf = (insn >> 31) & 1;
        opc = (insn >> 29) & 0x3;
        rm = (insn >> 16) & 0x1f;
        rn = (insn >> 5) & 0x1f;
        rd = insn & 0x1f;
        {
            int N;
            N = (insn >> 21) & 1;

            /* MOV is ORR with rn=XZR, N=0 */
            if (opc == 1 && rn == 31 && N == 0) {
                write_str(1, "mov\t");
                write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
                write_str(1, ", ");
                write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
                return 1;
            }

            /* MVN is ORN with rn=XZR */
            if (opc == 1 && rn == 31 && N == 1) {
                write_str(1, "mvn\t");
                write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
                write_str(1, ", ");
                write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
                return 1;
            }

            /* TST is ANDS with rd=XZR */
            if (opc == 3 && rd == 31) {
                write_str(1, "tst\t");
                write_str(1, sf ? reg_name_x(rn) : reg_name_w(rn));
                write_str(1, ", ");
                write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
                return 1;
            }

            switch (opc) {
            case 0: write_str(1, N ? "bic\t" : "and\t"); break;
            case 1: write_str(1, N ? "orn\t" : "orr\t"); break;
            case 2: write_str(1, N ? "eon\t" : "eor\t"); break;
            case 3: write_str(1, N ? "bics\t" : "ands\t"); break;
            }
        }
        write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_x(rn) : reg_name_w(rn));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
        return 1;
    }

    /* MUL / MADD / MSUB (Data processing 3-source) */
    if ((insn & 0x1F000000) == 0x1B000000) {
        sf = (insn >> 31) & 1;
        rm = (insn >> 16) & 0x1f;
        ra = (insn >> 10) & 0x1f;
        rn = (insn >> 5) & 0x1f;
        rd = insn & 0x1f;
        opc = (insn >> 15) & 1; /* o0 bit */

        if (opc == 0 && ra == 31) {
            /* MUL = MADD with ra=XZR */
            write_str(1, "mul\t");
        } else if (opc == 1 && ra == 31) {
            /* MNEG = MSUB with ra=XZR */
            write_str(1, "mneg\t");
        } else if (opc == 0) {
            write_str(1, "madd\t");
            write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
            write_str(1, ", ");
            write_str(1, sf ? reg_name_xzr(rn) : reg_name_w(rn));
            write_str(1, ", ");
            write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
            write_str(1, ", ");
            write_str(1, sf ? reg_name_xzr(ra) : reg_name_w(ra));
            return 1;
        } else {
            write_str(1, "msub\t");
            write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
            write_str(1, ", ");
            write_str(1, sf ? reg_name_xzr(rn) : reg_name_w(rn));
            write_str(1, ", ");
            write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
            write_str(1, ", ");
            write_str(1, sf ? reg_name_xzr(ra) : reg_name_w(ra));
            return 1;
        }
        write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_xzr(rn) : reg_name_w(rn));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
        return 1;
    }

    /* SDIV / UDIV (Data processing 2-source) */
    if ((insn & 0x1FE00000) == 0x1AC00000) {
        sf = (insn >> 31) & 1;
        rm = (insn >> 16) & 0x1f;
        opc = (insn >> 10) & 0x3f;
        rn = (insn >> 5) & 0x1f;
        rd = insn & 0x1f;

        if (opc == 0x03) {
            write_str(1, "sdiv\t");
        } else if (opc == 0x02) {
            write_str(1, "udiv\t");
        } else if (opc == 0x08) {
            write_str(1, "lsl\t");
        } else if (opc == 0x09) {
            write_str(1, "lsr\t");
        } else if (opc == 0x0A) {
            write_str(1, "asr\t");
        } else {
            write_str(1, ".inst\t0x");
            print_hex(insn, 8);
            return 1;
        }
        write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_xzr(rn) : reg_name_w(rn));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
        return 1;
    }

    /* LDR/STR unsigned immediate (64-bit) */
    if ((insn & 0x3F000000) == 0x39000000) {
        sf = (insn >> 30) & 0x3;  /* size field */
        opc = (insn >> 22) & 0x3;
        imm = (insn >> 10) & 0xfff;
        rn = (insn >> 5) & 0x1f;
        rt = insn & 0x1f;

        if (sf == 3 && opc == 1) {
            /* LDR Xt */
            write_str(1, "ldr\t");
            write_str(1, reg_name_x(rt));
            write_str(1, ", [");
            write_str(1, reg_name_x(rn));
            if (imm * 8 != 0) {
                write_str(1, ", #0x");
                print_hex((u64)(imm * 8), 1);
            }
            write_str(1, "]");
            return 1;
        }
        if (sf == 3 && opc == 0) {
            /* STR Xt */
            write_str(1, "str\t");
            write_str(1, reg_name_x(rt));
            write_str(1, ", [");
            write_str(1, reg_name_x(rn));
            if (imm * 8 != 0) {
                write_str(1, ", #0x");
                print_hex((u64)(imm * 8), 1);
            }
            write_str(1, "]");
            return 1;
        }
        if (sf == 2 && opc == 1) {
            /* LDR Wt */
            write_str(1, "ldr\t");
            write_str(1, reg_name_w(rt));
            write_str(1, ", [");
            write_str(1, reg_name_x(rn));
            if (imm * 4 != 0) {
                write_str(1, ", #0x");
                print_hex((u64)(imm * 4), 1);
            }
            write_str(1, "]");
            return 1;
        }
        if (sf == 2 && opc == 0) {
            /* STR Wt */
            write_str(1, "str\t");
            write_str(1, reg_name_w(rt));
            write_str(1, ", [");
            write_str(1, reg_name_x(rn));
            if (imm * 4 != 0) {
                write_str(1, ", #0x");
                print_hex((u64)(imm * 4), 1);
            }
            write_str(1, "]");
            return 1;
        }
        if (sf == 0 && opc == 1) {
            /* LDRB */
            write_str(1, "ldrb\t");
            write_str(1, reg_name_w(rt));
            write_str(1, ", [");
            write_str(1, reg_name_x(rn));
            if (imm != 0) {
                write_str(1, ", #0x");
                print_hex((u64)imm, 1);
            }
            write_str(1, "]");
            return 1;
        }
        if (sf == 0 && opc == 0) {
            /* STRB */
            write_str(1, "strb\t");
            write_str(1, reg_name_w(rt));
            write_str(1, ", [");
            write_str(1, reg_name_x(rn));
            if (imm != 0) {
                write_str(1, ", #0x");
                print_hex((u64)imm, 1);
            }
            write_str(1, "]");
            return 1;
        }
        if (sf == 1 && opc == 1) {
            /* LDRH */
            write_str(1, "ldrh\t");
            write_str(1, reg_name_w(rt));
            write_str(1, ", [");
            write_str(1, reg_name_x(rn));
            if (imm * 2 != 0) {
                write_str(1, ", #0x");
                print_hex((u64)(imm * 2), 1);
            }
            write_str(1, "]");
            return 1;
        }
        if (sf == 1 && opc == 0) {
            /* STRH */
            write_str(1, "strh\t");
            write_str(1, reg_name_w(rt));
            write_str(1, ", [");
            write_str(1, reg_name_x(rn));
            if (imm * 2 != 0) {
                write_str(1, ", #0x");
                print_hex((u64)(imm * 2), 1);
            }
            write_str(1, "]");
            return 1;
        }
        if (sf == 2 && opc == 2) {
            /* LDRSW */
            write_str(1, "ldrsw\t");
            write_str(1, reg_name_x(rt));
            write_str(1, ", [");
            write_str(1, reg_name_x(rn));
            if (imm * 4 != 0) {
                write_str(1, ", #0x");
                print_hex((u64)(imm * 4), 1);
            }
            write_str(1, "]");
            return 1;
        }
    }

    /* STP / LDP (signed offset) */
    if ((insn & 0xFFC00000) == 0x29000000 ||
        (insn & 0xFFC00000) == 0x29400000 ||
        (insn & 0xFFC00000) == 0xA9000000 ||
        (insn & 0xFFC00000) == 0xA9400000) {
        sf = (insn >> 31) & 1;
        opc = (insn >> 22) & 1; /* L bit: 0=STP, 1=LDP */
        imm = (insn >> 15) & 0x7f;
        rt2 = (insn >> 10) & 0x1f;
        rn = (insn >> 5) & 0x1f;
        rt = insn & 0x1f;
        offset = sign_extend(imm, 7) * (sf ? 8 : 4);

        write_str(1, opc ? "ldp\t" : "stp\t");
        write_str(1, sf ? reg_name_x(rt) : reg_name_w(rt));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_x(rt2) : reg_name_w(rt2));
        write_str(1, ", [");
        write_str(1, reg_name_x(rn));
        if (offset != 0) {
            write_str(1, ", #");
            print_sdec(offset);
        }
        write_str(1, "]");
        return 1;
    }

    /* STP pre-index / LDP post-index */
    if ((insn & 0xFFC00000) == 0x29800000 ||
        (insn & 0xFFC00000) == 0xA9800000) {
        /* STP pre-index */
        sf = (insn >> 31) & 1;
        imm = (insn >> 15) & 0x7f;
        rt2 = (insn >> 10) & 0x1f;
        rn = (insn >> 5) & 0x1f;
        rt = insn & 0x1f;
        offset = sign_extend(imm, 7) * (sf ? 8 : 4);

        write_str(1, "stp\t");
        write_str(1, sf ? reg_name_x(rt) : reg_name_w(rt));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_x(rt2) : reg_name_w(rt2));
        write_str(1, ", [");
        write_str(1, reg_name_x(rn));
        write_str(1, ", #");
        print_sdec(offset);
        write_str(1, "]!");
        return 1;
    }
    if ((insn & 0xFFC00000) == 0x28C00000 ||
        (insn & 0xFFC00000) == 0xA8C00000) {
        /* LDP post-index */
        sf = (insn >> 31) & 1;
        imm = (insn >> 15) & 0x7f;
        rt2 = (insn >> 10) & 0x1f;
        rn = (insn >> 5) & 0x1f;
        rt = insn & 0x1f;
        offset = sign_extend(imm, 7) * (sf ? 8 : 4);

        write_str(1, "ldp\t");
        write_str(1, sf ? reg_name_x(rt) : reg_name_w(rt));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_x(rt2) : reg_name_w(rt2));
        write_str(1, ", [");
        write_str(1, reg_name_x(rn));
        write_str(1, "], #");
        print_sdec(offset);
        return 1;
    }

    /* ADRP */
    if ((insn & 0x9F000000) == 0x90000000) {
        i64 page_offset;
        u64 immlo;
        u64 immhi;
        rd = insn & 0x1f;
        immlo = (insn >> 29) & 0x3;
        immhi = (insn >> 5) & 0x7FFFF;
        page_offset = sign_extend((immhi << 2) | immlo, 21) * 4096;
        write_str(1, "adrp\t");
        write_str(1, reg_name_x(rd));
        write_str(1, ", ");
        print_hex((u64)((i64)(addr & ~0xFFF) + page_offset), 1);
        return 1;
    }

    /* ADR */
    if ((insn & 0x9F000000) == 0x10000000) {
        u64 immlo;
        u64 immhi;
        rd = insn & 0x1f;
        immlo = (insn >> 29) & 0x3;
        immhi = (insn >> 5) & 0x7FFFF;
        offset = sign_extend((immhi << 2) | immlo, 21);
        write_str(1, "adr\t");
        write_str(1, reg_name_x(rd));
        write_str(1, ", ");
        print_hex((u64)((i64)addr + offset), 1);
        return 1;
    }

    /* CSET (CSINC rd, XZR, XZR, inv_cond) */
    if ((insn & 0x7FE00C00) == 0x1A800400) {
        int cond_field;
        sf = (insn >> 31) & 1;
        rd = insn & 0x1f;
        rn = (insn >> 5) & 0x1f;
        rm = (insn >> 16) & 0x1f;
        cond_field = (insn >> 12) & 0xf;

        if (rn == 31 && rm == 31) {
            write_str(1, "cset\t");
            write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
            write_str(1, ", ");
            write_str(1, cond_name(cond_field ^ 1));
            return 1;
        }

        write_str(1, "csinc\t");
        write_str(1, sf ? reg_name_xzr(rd) : reg_name_w(rd));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_xzr(rn) : reg_name_w(rn));
        write_str(1, ", ");
        write_str(1, sf ? reg_name_xzr(rm) : reg_name_w(rm));
        write_str(1, ", ");
        write_str(1, cond_name(cond_field));
        return 1;
    }

    /* LDR (literal) - PC-relative */
    if ((insn & 0xFF000000) == 0x58000000) {
        rt = insn & 0x1f;
        offset = sign_extend((insn >> 5) & 0x7FFFF, 19) * 4;
        write_str(1, "ldr\t");
        write_str(1, reg_name_x(rt));
        write_str(1, ", ");
        print_hex((u64)((i64)addr + offset), 1);
        return 1;
    }

    /* Unknown instruction */
    write_str(1, ".inst\t0x");
    print_hex(insn, 8);
    return 0;
}

/* ---- disassemble .text section ---- */
static void display_disassembly(void)
{
    int i;
    int j;
    const Elf64_Shdr *sh;
    const u32 *code;
    u64 addr;
    int n_insns;

    for (i = 1; i < elf_ehdr->e_shnum; i++) {
        sh = &elf_shdrs[i];
        if (!(sh->sh_flags & SHF_EXECINSTR)) {
            continue;
        }
        if (sh->sh_type == SHT_NOBITS) {
            continue;
        }

        write_str(1, "\nDisassembly of section ");
        write_str(1, section_name(i));
        write_str(1, ":\n\n");

        code = (const u32 *)(elf_data + sh->sh_offset);
        addr = sh->sh_addr;
        n_insns = (int)(sh->sh_size / 4);

        for (j = 0; j < n_insns; j++) {
            /* Address */
            write_str(1, "  ");
            print_hex(addr, 8);
            write_str(1, ":\t");

            /* Raw instruction bytes (little-endian) */
            print_hex(code[j] & 0xff, 2);
            write_str(1, " ");
            print_hex((code[j] >> 8) & 0xff, 2);
            write_str(1, " ");
            print_hex((code[j] >> 16) & 0xff, 2);
            write_str(1, " ");
            print_hex((code[j] >> 24) & 0xff, 2);
            write_str(1, "\t");

            /* Disassemble */
            disasm_insn(code[j], addr);
            write_str(1, "\n");

            addr += 4;
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
    int opt_d;
    int opt_t;
    int opt_h;
    int opt_r;
    int opt_s;
    const char *filename;
    int i;
    const char *arg;

    /* Handle --version early */
    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "--version")) {
            write_str(1, "GNU objdump (free-objdump) 2.42\n");
            sys_exit(0);
        }
    }

    opt_d = 0;
    opt_t = 0;
    opt_h = 0;
    opt_r = 0;
    opt_s = 0;
    filename = NULL;

    for (i = 1; i < argc; i++) {
        arg = argv[i];
        if (arg[0] == '-') {
            /* Parse flags (may be combined like -dhtr) */
            const char *p;
            p = arg + 1;
            while (*p) {
                switch (*p) {
                case 'd': opt_d = 1; break;
                case 't': opt_t = 1; break;
                case 'h': opt_h = 1; break;
                case 'r': opt_r = 1; break;
                case 's': opt_s = 1; break;
                default:
                    write_str(2, "free-objdump: unknown option '-");
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
        write_str(2, "Usage: free-objdump [-d] [-t] [-h] [-r] [-s] file\n");
        sys_exit(1);
    }

    /* Read and parse ELF */
    elf_size = read_file(filename, file_buf, BUF_SIZE);
    elf_data = file_buf;
    parse_elf();

    write_str(1, "\n");
    write_str(1, filename);
    write_str(1, ":     file format elf64-littleaarch64\n");

    if (opt_h) {
        display_headers();
    }
    if (opt_t) {
        display_symbols();
    }
    if (opt_r) {
        display_relocations();
    }
    if (opt_s) {
        display_contents();
    }
    if (opt_d) {
        display_disassembly();
    }

    sys_exit(0);
    return 0;
}
