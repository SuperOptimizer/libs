/*
 * test_emit.c - Tests for ELF object file emission.
 * Assembles simple instruction sequences and verifies the resulting
 * ELF object file has correct headers, sections, and code bytes.
 * Pure C89. No external dependencies beyond stdio/string.
 */

#include "../test.h"
#include "aarch64.h"
#include "elf.h"
#include <stdio.h>
#include <string.h>

/*
 * We test ELF emission by writing a minimal .o file to a temp path,
 * then reading it back and inspecting the binary contents.
 *
 * The program under test: "mov x0, #42; ret"
 *   MOVZ X0, #42  => 0xD2800540
 *   RET            => 0xD65F03C0
 */

/* Write a minimal ELF relocatable object with a .text section */
static int write_test_object(const char *path)
{
    FILE *f;
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdrs[5];    /* null, .text, .symtab, .strtab, .shstrtab */
    Elf64_Sym syms[2];       /* null symbol + _start */
    u32 code[2];
    /*
     * Section name strings:
     * .shstrtab contents: \0 .text\0 .symtab\0 .strtab\0 .shstrtab\0
     * offsets:             0  1       7         15        23
     */
    static const char shstrtab[] =
        "\0.text\0.symtab\0.strtab\0.shstrtab";
    /*
     * .strtab contents: \0 _start\0
     * offsets:           0  1
     */
    static const char strtab[] = "\0_start";

    u64 ehdr_size;
    u64 text_off;
    u64 shstrtab_off;
    u64 strtab_off;
    u64 symtab_off;
    u64 shdr_off;

    /* Instruction bytes (little-endian) */
    code[0] = a64_movz(REG_X0, 42, 0);   /* 0xD2800540 */
    code[1] = a64_ret();                   /* 0xD65F03C0 */

    /* Compute layout */
    ehdr_size = sizeof(Elf64_Ehdr);    /* 64 */
    text_off = ehdr_size;               /* .text right after ELF header */
    shstrtab_off = text_off + 8;        /* 8 bytes of code */
    strtab_off = shstrtab_off + sizeof(shstrtab);
    symtab_off = strtab_off + sizeof(strtab);
    shdr_off = symtab_off + sizeof(syms);

    /* Align shdr_off to 8 bytes */
    shdr_off = (shdr_off + 7) & ~(u64)7;

    /* ELF header */
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_ident[7] = ELFOSABI_NONE;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_AARCH64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = 0;
    ehdr.e_phoff = 0;
    ehdr.e_shoff = shdr_off;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = 0;
    ehdr.e_phnum = 0;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = 5;
    ehdr.e_shstrndx = 4;     /* .shstrtab is section 4 */

    /* Section headers */
    memset(shdrs, 0, sizeof(shdrs));

    /* [0] SHT_NULL */
    /* already zeroed */

    /* [1] .text */
    shdrs[1].sh_name = 1;    /* offset of ".text" in shstrtab */
    shdrs[1].sh_type = SHT_PROGBITS;
    shdrs[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[1].sh_addr = 0;
    shdrs[1].sh_offset = text_off;
    shdrs[1].sh_size = 8;    /* 2 instructions * 4 bytes */
    shdrs[1].sh_addralign = 4;
    shdrs[1].sh_entsize = 0;

    /* [2] .symtab */
    shdrs[2].sh_name = 7;    /* offset of ".symtab" in shstrtab */
    shdrs[2].sh_type = SHT_SYMTAB;
    shdrs[2].sh_flags = 0;
    shdrs[2].sh_offset = symtab_off;
    shdrs[2].sh_size = sizeof(syms);
    shdrs[2].sh_link = 3;    /* .strtab section index */
    shdrs[2].sh_info = 1;    /* first global symbol index */
    shdrs[2].sh_addralign = 8;
    shdrs[2].sh_entsize = sizeof(Elf64_Sym);

    /* [3] .strtab */
    shdrs[3].sh_name = 15;   /* offset of ".strtab" in shstrtab */
    shdrs[3].sh_type = SHT_STRTAB;
    shdrs[3].sh_flags = 0;
    shdrs[3].sh_offset = strtab_off;
    shdrs[3].sh_size = sizeof(strtab);
    shdrs[3].sh_addralign = 1;

    /* [4] .shstrtab */
    shdrs[4].sh_name = 23;   /* offset of ".shstrtab" in shstrtab */
    shdrs[4].sh_type = SHT_STRTAB;
    shdrs[4].sh_flags = 0;
    shdrs[4].sh_offset = shstrtab_off;
    shdrs[4].sh_size = sizeof(shstrtab);
    shdrs[4].sh_addralign = 1;

    /* Symbol table */
    memset(syms, 0, sizeof(syms));
    /* [0] null symbol (already zeroed) */
    /* [1] _start */
    syms[1].st_name = 1;     /* offset in strtab */
    syms[1].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    syms[1].st_other = 0;
    syms[1].st_shndx = 1;    /* .text section */
    syms[1].st_value = 0;
    syms[1].st_size = 8;

    /* Write the file */
    f = fopen(path, "wb");
    if (!f) {
        return -1;
    }

    fwrite(&ehdr, sizeof(ehdr), 1, f);
    fwrite(code, sizeof(code), 1, f);
    fwrite(shstrtab, sizeof(shstrtab), 1, f);
    fwrite(strtab, sizeof(strtab), 1, f);
    fwrite(syms, sizeof(syms), 1, f);

    /* Pad to shdr_off if needed */
    {
        long cur = (long)ftell(f);
        while (cur < (long)shdr_off) {
            fputc(0, f);
            cur++;
        }
    }

    fwrite(shdrs, sizeof(shdrs), 1, f);
    fclose(f);
    return 0;
}

/* ===== ELF header tests ===== */

TEST(elf_magic)
{
    unsigned char buf[16];
    FILE *f;

    ASSERT_EQ(write_test_object("/tmp/test_emit.o"), 0);

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);

    ASSERT_EQ(fread(buf, 1, 16, f), 16);
    fclose(f);

    ASSERT_EQ(buf[0], 0x7f);
    ASSERT_EQ(buf[1], 'E');
    ASSERT_EQ(buf[2], 'L');
    ASSERT_EQ(buf[3], 'F');
}

TEST(elf_class)
{
    Elf64_Ehdr ehdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);

    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(ehdr.e_ident[4], ELFCLASS64);
}

TEST(elf_data_encoding)
{
    Elf64_Ehdr ehdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(ehdr.e_ident[5], ELFDATA2LSB);
}

TEST(elf_version)
{
    Elf64_Ehdr ehdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(ehdr.e_ident[6], EV_CURRENT);
    ASSERT_EQ(ehdr.e_version, EV_CURRENT);
}

TEST(elf_type_rel)
{
    Elf64_Ehdr ehdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(ehdr.e_type, ET_REL);
}

TEST(elf_machine_aarch64)
{
    Elf64_Ehdr ehdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(ehdr.e_machine, EM_AARCH64);
}

TEST(elf_ehsize)
{
    Elf64_Ehdr ehdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(ehdr.e_ehsize, sizeof(Elf64_Ehdr));
}

TEST(elf_section_count)
{
    Elf64_Ehdr ehdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fclose(f);

    /* null + .text + .symtab + .strtab + .shstrtab = 5 */
    ASSERT_EQ(ehdr.e_shnum, 5);
}

TEST(elf_shstrndx)
{
    Elf64_Ehdr ehdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(ehdr.e_shstrndx, 4);
}

/* ===== Section header tests ===== */

TEST(section_null)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fseek(f, (long)ehdr.e_shoff, SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(shdr.sh_type, SHT_NULL);
}

TEST(section_text_type)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    /* .text is section 1 */
    fseek(f, (long)(ehdr.e_shoff + ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(shdr.sh_type, SHT_PROGBITS);
}

TEST(section_text_flags)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fseek(f, (long)(ehdr.e_shoff + ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fclose(f);

    ASSERT(shdr.sh_flags & SHF_ALLOC);
    ASSERT(shdr.sh_flags & SHF_EXECINSTR);
}

TEST(section_text_size)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fseek(f, (long)(ehdr.e_shoff + ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fclose(f);

    /* 2 instructions * 4 bytes = 8 */
    ASSERT_EQ(shdr.sh_size, 8);
}

TEST(section_symtab_type)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    /* .symtab is section 2 */
    fseek(f, (long)(ehdr.e_shoff + 2 * ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(shdr.sh_type, SHT_SYMTAB);
}

TEST(section_symtab_entsize)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fseek(f, (long)(ehdr.e_shoff + 2 * ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(shdr.sh_entsize, sizeof(Elf64_Sym));
}

TEST(section_symtab_link)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fseek(f, (long)(ehdr.e_shoff + 2 * ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fclose(f);

    /* sh_link should point to .strtab (section 3) */
    ASSERT_EQ(shdr.sh_link, 3);
}

TEST(section_strtab_type)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    /* .strtab is section 3 */
    fseek(f, (long)(ehdr.e_shoff + 3 * ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(shdr.sh_type, SHT_STRTAB);
}

TEST(section_shstrtab_type)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    /* .shstrtab is section 4 */
    fseek(f, (long)(ehdr.e_shoff + 4 * ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(shdr.sh_type, SHT_STRTAB);
}

/* ===== Code content tests ===== */

TEST(text_contains_movz)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    u32 insn;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    /* Read .text section header */
    fseek(f, (long)(ehdr.e_shoff + ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    /* Read first instruction */
    fseek(f, (long)shdr.sh_offset, SEEK_SET);
    ASSERT_EQ(fread(&insn, sizeof(insn), 1, f), 1);
    fclose(f);

    /* MOVZ X0, #42 = 0xD2800540 */
    ASSERT_EQ(insn, (long)0xD2800540);
}

TEST(text_contains_ret)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    u32 insns[2];
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fseek(f, (long)(ehdr.e_shoff + ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fseek(f, (long)shdr.sh_offset, SEEK_SET);
    ASSERT_EQ(fread(insns, sizeof(u32), 2, f), 2);
    fclose(f);

    /* RET = 0xD65F03C0 */
    ASSERT_EQ(insns[1], (long)0xD65F03C0);
}

/* ===== Symbol table tests ===== */

TEST(symtab_null_entry)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    Elf64_Sym sym;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    /* .symtab is section 2 */
    fseek(f, (long)(ehdr.e_shoff + 2 * ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    /* Read first symbol (null entry) */
    fseek(f, (long)shdr.sh_offset, SEEK_SET);
    ASSERT_EQ(fread(&sym, sizeof(sym), 1, f), 1);
    fclose(f);

    ASSERT_EQ(sym.st_name, 0);
    ASSERT_EQ(sym.st_info, 0);
    ASSERT_EQ(sym.st_value, 0);
    ASSERT_EQ(sym.st_size, 0);
}

TEST(symtab_start_symbol)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    Elf64_Sym syms[2];
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fseek(f, (long)(ehdr.e_shoff + 2 * ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fseek(f, (long)shdr.sh_offset, SEEK_SET);
    ASSERT_EQ(fread(syms, sizeof(Elf64_Sym), 2, f), 2);
    fclose(f);

    /* _start symbol */
    ASSERT_EQ(syms[1].st_name, 1);   /* offset in strtab */
    ASSERT_EQ(ELF64_ST_BIND(syms[1].st_info), STB_GLOBAL);
    ASSERT_EQ(ELF64_ST_TYPE(syms[1].st_info), STT_FUNC);
    ASSERT_EQ(syms[1].st_shndx, 1);  /* .text section */
    ASSERT_EQ(syms[1].st_value, 0);
    ASSERT_EQ(syms[1].st_size, 8);
}

/* ===== String table tests ===== */

TEST(strtab_contains_start)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    char buf[32];
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    /* .strtab is section 3 */
    fseek(f, (long)(ehdr.e_shoff + 3 * ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fseek(f, (long)shdr.sh_offset, SEEK_SET);
    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(fread(buf, 1, (int)shdr.sh_size, f), (long)shdr.sh_size);
    fclose(f);

    /* First byte must be NUL */
    ASSERT_EQ(buf[0], '\0');
    /* "_start" at offset 1 */
    ASSERT_STR_EQ(buf + 1, "_start");
}

TEST(shstrtab_contains_section_names)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    char buf[64];
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    /* .shstrtab is section 4 */
    fseek(f, (long)(ehdr.e_shoff + 4 * ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fseek(f, (long)shdr.sh_offset, SEEK_SET);
    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(fread(buf, 1, (int)shdr.sh_size, f), (long)shdr.sh_size);
    fclose(f);

    /* Check that section names are present at correct offsets */
    ASSERT_EQ(buf[0], '\0');
    ASSERT_STR_EQ(buf + 1, ".text");
    ASSERT_STR_EQ(buf + 7, ".symtab");
    ASSERT_STR_EQ(buf + 15, ".strtab");
    ASSERT_STR_EQ(buf + 23, ".shstrtab");
}

/* ===== Section name verification through shstrtab ===== */

TEST(section_names_match_shstrtab)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdrs[5];
    Elf64_Shdr shstrtab_shdr;
    char names[64];
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);

    /* Read all section headers */
    fseek(f, (long)ehdr.e_shoff, SEEK_SET);
    ASSERT_EQ(fread(shdrs, sizeof(Elf64_Shdr), 5, f), 5);

    /* Read shstrtab contents */
    shstrtab_shdr = shdrs[ehdr.e_shstrndx];
    fseek(f, (long)shstrtab_shdr.sh_offset, SEEK_SET);
    memset(names, 0, sizeof(names));
    ASSERT_EQ(fread(names, 1, (int)shstrtab_shdr.sh_size, f),
              (long)shstrtab_shdr.sh_size);
    fclose(f);

    /* Verify each section's name */
    ASSERT_STR_EQ(names + shdrs[1].sh_name, ".text");
    ASSERT_STR_EQ(names + shdrs[2].sh_name, ".symtab");
    ASSERT_STR_EQ(names + shdrs[3].sh_name, ".strtab");
    ASSERT_STR_EQ(names + shdrs[4].sh_name, ".shstrtab");
}

/* ===== ELF validity: no program headers for .o ===== */

TEST(elf_no_program_headers)
{
    Elf64_Ehdr ehdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(ehdr.e_phoff, 0);
    ASSERT_EQ(ehdr.e_phnum, 0);
}

/* ===== ELF section header entry size ===== */

TEST(elf_shentsize)
{
    Elf64_Ehdr ehdr;
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fclose(f);

    ASSERT_EQ(ehdr.e_shentsize, sizeof(Elf64_Shdr));
}

/* ===== Code byte order (little-endian) ===== */

TEST(text_byte_order)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    unsigned char bytes[8];
    FILE *f;

    f = fopen("/tmp/test_emit.o", "rb");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    fseek(f, (long)(ehdr.e_shoff + ehdr.e_shentsize), SEEK_SET);
    ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    fseek(f, (long)shdr.sh_offset, SEEK_SET);
    ASSERT_EQ(fread(bytes, 1, 8, f), 8);
    fclose(f);

    /*
     * MOVZ X0, #42 = 0xD2800540 in little-endian:
     * byte[0]=0x40, byte[1]=0x05, byte[2]=0x80, byte[3]=0xD2
     */
    ASSERT_EQ(bytes[0], 0x40);
    ASSERT_EQ(bytes[1], 0x05);
    ASSERT_EQ(bytes[2], 0x80);
    ASSERT_EQ(bytes[3], 0xD2);

    /*
     * RET = 0xD65F03C0 in little-endian:
     * byte[4]=0xC0, byte[5]=0x03, byte[6]=0x5F, byte[7]=0xD6
     */
    ASSERT_EQ(bytes[4], 0xC0);
    ASSERT_EQ(bytes[5], 0x03);
    ASSERT_EQ(bytes[6], 0x5F);
    ASSERT_EQ(bytes[7], 0xD6);
}

int main(void)
{
    printf("test_emit:\n");

    /* Generate the test object file first */
    if (write_test_object("/tmp/test_emit.o") != 0) {
        printf("  FAIL: could not write test object file\n");
        return 1;
    }

    /* ELF header */
    RUN_TEST(elf_magic);
    RUN_TEST(elf_class);
    RUN_TEST(elf_data_encoding);
    RUN_TEST(elf_version);
    RUN_TEST(elf_type_rel);
    RUN_TEST(elf_machine_aarch64);
    RUN_TEST(elf_ehsize);
    RUN_TEST(elf_section_count);
    RUN_TEST(elf_shstrndx);
    RUN_TEST(elf_no_program_headers);
    RUN_TEST(elf_shentsize);

    /* Section headers */
    RUN_TEST(section_null);
    RUN_TEST(section_text_type);
    RUN_TEST(section_text_flags);
    RUN_TEST(section_text_size);
    RUN_TEST(section_symtab_type);
    RUN_TEST(section_symtab_entsize);
    RUN_TEST(section_symtab_link);
    RUN_TEST(section_strtab_type);
    RUN_TEST(section_shstrtab_type);

    /* Code content */
    RUN_TEST(text_contains_movz);
    RUN_TEST(text_contains_ret);
    RUN_TEST(text_byte_order);

    /* Symbol table */
    RUN_TEST(symtab_null_entry);
    RUN_TEST(symtab_start_symbol);

    /* String tables */
    RUN_TEST(strtab_contains_start);
    RUN_TEST(shstrtab_contains_section_names);
    RUN_TEST(section_names_match_shstrtab);

    TEST_SUMMARY();
    return tests_failed;
}
