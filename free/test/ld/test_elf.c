/*
 * test_elf.c - Tests for ELF64 header, section, and symbol construction.
 * Verifies in-memory ELF structure creation, magic bytes, field layout,
 * section header parsing, and symbol table interpretation.
 * Pure C89.
 */

#include "../test.h"
#include <string.h>
#include <stdlib.h>

/*
 * Pull in the ELF types from the toolchain's own header.
 * free.h provides u8, u16, u32, u64, i64 etc.
 * elf.h provides Elf64_Ehdr, Elf64_Phdr, Elf64_Shdr, Elf64_Sym, Elf64_Rela,
 * and all associated constants.
 */
#include "free.h"
#include "elf.h"

/* ===== helpers ===== */

/*
 * Build a valid ELF64 relocatable-object header for aarch64.
 * Caller may overwrite fields afterwards.
 */
static void make_rel_ehdr(Elf64_Ehdr *e)
{
    memset(e, 0, sizeof(*e));
    e->e_ident[0] = ELFMAG0;
    e->e_ident[1] = ELFMAG1;
    e->e_ident[2] = ELFMAG2;
    e->e_ident[3] = ELFMAG3;
    e->e_ident[4] = ELFCLASS64;
    e->e_ident[5] = ELFDATA2LSB;
    e->e_ident[6] = EV_CURRENT;
    e->e_ident[7] = ELFOSABI_NONE;
    e->e_type     = ET_REL;
    e->e_machine  = EM_AARCH64;
    e->e_version  = EV_CURRENT;
    e->e_entry    = 0;
    e->e_ehsize   = (u16)sizeof(Elf64_Ehdr);
    e->e_shentsize = (u16)sizeof(Elf64_Shdr);
}

/*
 * Build a valid ELF64 executable header for aarch64.
 */
static void make_exec_ehdr(Elf64_Ehdr *e, u64 entry)
{
    make_rel_ehdr(e);
    e->e_type      = ET_EXEC;
    e->e_entry     = entry;
    e->e_phentsize = (u16)sizeof(Elf64_Phdr);
}

/* ===== ELF header field verification ===== */

TEST(ehdr_magic_bytes)
{
    Elf64_Ehdr e;

    make_rel_ehdr(&e);
    ASSERT_EQ(e.e_ident[0], 0x7f);
    ASSERT_EQ(e.e_ident[1], 'E');
    ASSERT_EQ(e.e_ident[2], 'L');
    ASSERT_EQ(e.e_ident[3], 'F');
}

TEST(ehdr_class_and_data)
{
    Elf64_Ehdr e;

    make_rel_ehdr(&e);
    ASSERT_EQ(e.e_ident[4], ELFCLASS64);
    ASSERT_EQ(e.e_ident[5], ELFDATA2LSB);
}

TEST(ehdr_version)
{
    Elf64_Ehdr e;

    make_rel_ehdr(&e);
    ASSERT_EQ(e.e_ident[6], EV_CURRENT);
    ASSERT_EQ(e.e_version, EV_CURRENT);
}

TEST(ehdr_osabi)
{
    Elf64_Ehdr e;

    make_rel_ehdr(&e);
    ASSERT_EQ(e.e_ident[7], ELFOSABI_NONE);
}

TEST(ehdr_type_rel)
{
    Elf64_Ehdr e;

    make_rel_ehdr(&e);
    ASSERT_EQ(e.e_type, ET_REL);
}

TEST(ehdr_type_exec)
{
    Elf64_Ehdr e;

    make_exec_ehdr(&e, 0x400000);
    ASSERT_EQ(e.e_type, ET_EXEC);
}

TEST(ehdr_machine)
{
    Elf64_Ehdr e;

    make_rel_ehdr(&e);
    ASSERT_EQ(e.e_machine, EM_AARCH64);
}

TEST(ehdr_entry_zero_for_rel)
{
    Elf64_Ehdr e;

    make_rel_ehdr(&e);
    ASSERT_EQ(e.e_entry, 0);
}

TEST(ehdr_entry_for_exec)
{
    Elf64_Ehdr e;

    make_exec_ehdr(&e, 0x400080);
    ASSERT_EQ(e.e_entry, 0x400080);
}

TEST(ehdr_sizes)
{
    Elf64_Ehdr e;

    make_rel_ehdr(&e);
    ASSERT_EQ(e.e_ehsize, sizeof(Elf64_Ehdr));
    ASSERT_EQ(e.e_shentsize, sizeof(Elf64_Shdr));
}

TEST(ehdr_phentsize_exec)
{
    Elf64_Ehdr e;

    make_exec_ehdr(&e, 0);
    ASSERT_EQ(e.e_phentsize, sizeof(Elf64_Phdr));
}

TEST(ehdr_padding_zeroed)
{
    Elf64_Ehdr e;
    int i;

    make_rel_ehdr(&e);
    /* e_ident[8..15] must be zero padding */
    for (i = 8; i < EI_NIDENT; i++) {
        ASSERT_EQ(e.e_ident[i], 0);
    }
}

/* ===== raw bytes round-trip ===== */

TEST(ehdr_raw_magic_at_offset_zero)
{
    Elf64_Ehdr e;
    u8 *raw;

    make_rel_ehdr(&e);
    raw = (u8 *)&e;
    /* first four bytes of the on-disk format are the magic */
    ASSERT_EQ(raw[0], 0x7f);
    ASSERT_EQ(raw[1], 'E');
    ASSERT_EQ(raw[2], 'L');
    ASSERT_EQ(raw[3], 'F');
}

TEST(ehdr_struct_size)
{
    /* ELF64 header is always 64 bytes */
    ASSERT_EQ(sizeof(Elf64_Ehdr), 64);
}

TEST(phdr_struct_size)
{
    /* ELF64 program header entry is always 56 bytes */
    ASSERT_EQ(sizeof(Elf64_Phdr), 56);
}

TEST(shdr_struct_size)
{
    /* ELF64 section header entry is always 64 bytes */
    ASSERT_EQ(sizeof(Elf64_Shdr), 64);
}

TEST(sym_struct_size)
{
    /* ELF64 symbol entry is always 24 bytes */
    ASSERT_EQ(sizeof(Elf64_Sym), 24);
}

TEST(rela_struct_size)
{
    /* ELF64 rela entry is always 24 bytes */
    ASSERT_EQ(sizeof(Elf64_Rela), 24);
}

/* ===== section header construction ===== */

TEST(shdr_null_section)
{
    Elf64_Shdr sh;

    memset(&sh, 0, sizeof(sh));
    ASSERT_EQ(sh.sh_name, 0);
    ASSERT_EQ(sh.sh_type, SHT_NULL);
    ASSERT_EQ(sh.sh_flags, 0);
    ASSERT_EQ(sh.sh_addr, 0);
    ASSERT_EQ(sh.sh_offset, 0);
    ASSERT_EQ(sh.sh_size, 0);
}

TEST(shdr_text_section)
{
    Elf64_Shdr sh;

    memset(&sh, 0, sizeof(sh));
    sh.sh_name      = 1;
    sh.sh_type      = SHT_PROGBITS;
    sh.sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
    sh.sh_addralign = 4;
    sh.sh_size      = 0x100;

    ASSERT_EQ(sh.sh_type, SHT_PROGBITS);
    ASSERT_EQ(sh.sh_flags, SHF_ALLOC | SHF_EXECINSTR);
    ASSERT_EQ(sh.sh_addralign, 4);
    ASSERT_EQ(sh.sh_size, 0x100);
}

TEST(shdr_data_section)
{
    Elf64_Shdr sh;

    memset(&sh, 0, sizeof(sh));
    sh.sh_type  = SHT_PROGBITS;
    sh.sh_flags = SHF_ALLOC | SHF_WRITE;
    sh.sh_addralign = 8;

    ASSERT_EQ(sh.sh_flags, SHF_ALLOC | SHF_WRITE);
    ASSERT(!(sh.sh_flags & SHF_EXECINSTR));
}

TEST(shdr_bss_section)
{
    Elf64_Shdr sh;

    memset(&sh, 0, sizeof(sh));
    sh.sh_type  = SHT_NOBITS;
    sh.sh_flags = SHF_ALLOC | SHF_WRITE;
    sh.sh_size  = 0x200;

    ASSERT_EQ(sh.sh_type, SHT_NOBITS);
    /* NOBITS occupies no file space but has a virtual size */
    ASSERT_EQ(sh.sh_size, 0x200);
}

TEST(shdr_symtab_section)
{
    Elf64_Shdr sh;

    memset(&sh, 0, sizeof(sh));
    sh.sh_type    = SHT_SYMTAB;
    sh.sh_link    = 3;  /* index of associated strtab */
    sh.sh_info    = 2;  /* first non-local symbol index */
    sh.sh_entsize = sizeof(Elf64_Sym);

    ASSERT_EQ(sh.sh_type, SHT_SYMTAB);
    ASSERT_EQ(sh.sh_link, 3);
    ASSERT_EQ(sh.sh_info, 2);
    ASSERT_EQ(sh.sh_entsize, sizeof(Elf64_Sym));
}

TEST(shdr_strtab_section)
{
    Elf64_Shdr sh;

    memset(&sh, 0, sizeof(sh));
    sh.sh_type      = SHT_STRTAB;
    sh.sh_addralign = 1;
    sh.sh_size      = 32;

    ASSERT_EQ(sh.sh_type, SHT_STRTAB);
    ASSERT_EQ(sh.sh_addralign, 1);
}

TEST(shdr_rela_section)
{
    Elf64_Shdr sh;

    memset(&sh, 0, sizeof(sh));
    sh.sh_type    = SHT_RELA;
    sh.sh_flags   = SHF_INFO_LINK;
    sh.sh_link    = 2;  /* symtab index */
    sh.sh_info    = 1;  /* section to which relocs apply */
    sh.sh_entsize = sizeof(Elf64_Rela);

    ASSERT_EQ(sh.sh_type, SHT_RELA);
    ASSERT_EQ(sh.sh_flags, SHF_INFO_LINK);
    ASSERT_EQ(sh.sh_link, 2);
    ASSERT_EQ(sh.sh_info, 1);
    ASSERT_EQ(sh.sh_entsize, sizeof(Elf64_Rela));
}

/* ===== symbol table construction ===== */

TEST(sym_null_entry)
{
    Elf64_Sym sym;

    memset(&sym, 0, sizeof(sym));
    ASSERT_EQ(sym.st_name, 0);
    ASSERT_EQ(sym.st_info, 0);
    ASSERT_EQ(sym.st_other, 0);
    ASSERT_EQ(sym.st_shndx, SHN_UNDEF);
    ASSERT_EQ(sym.st_value, 0);
    ASSERT_EQ(sym.st_size, 0);
}

TEST(sym_global_func)
{
    Elf64_Sym sym;

    memset(&sym, 0, sizeof(sym));
    sym.st_name  = 5;
    sym.st_info  = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    sym.st_shndx = 1;
    sym.st_value = 0;
    sym.st_size  = 0x40;

    ASSERT_EQ(ELF64_ST_BIND(sym.st_info), STB_GLOBAL);
    ASSERT_EQ(ELF64_ST_TYPE(sym.st_info), STT_FUNC);
    ASSERT_EQ(sym.st_shndx, 1);
    ASSERT_EQ(sym.st_size, 0x40);
}

TEST(sym_local_notype)
{
    Elf64_Sym sym;

    memset(&sym, 0, sizeof(sym));
    sym.st_info  = ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE);
    sym.st_shndx = 1;
    sym.st_value = 0x10;

    ASSERT_EQ(ELF64_ST_BIND(sym.st_info), STB_LOCAL);
    ASSERT_EQ(ELF64_ST_TYPE(sym.st_info), STT_NOTYPE);
}

TEST(sym_section_symbol)
{
    Elf64_Sym sym;

    memset(&sym, 0, sizeof(sym));
    sym.st_info  = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
    sym.st_shndx = 1;

    ASSERT_EQ(ELF64_ST_TYPE(sym.st_info), STT_SECTION);
}

TEST(sym_undefined_reference)
{
    Elf64_Sym sym;

    memset(&sym, 0, sizeof(sym));
    sym.st_name  = 10;
    sym.st_info  = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
    sym.st_shndx = SHN_UNDEF;

    ASSERT_EQ(sym.st_shndx, SHN_UNDEF);
    ASSERT_EQ(ELF64_ST_BIND(sym.st_info), STB_GLOBAL);
}

TEST(sym_absolute_value)
{
    Elf64_Sym sym;

    memset(&sym, 0, sizeof(sym));
    sym.st_info  = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
    sym.st_shndx = SHN_ABS;
    sym.st_value = 42;

    ASSERT_EQ(sym.st_shndx, SHN_ABS);
    ASSERT_EQ(sym.st_value, 42);
}

TEST(sym_info_encoding)
{
    /* ST_INFO packs binding in the high 4 bits, type in the low 4 */
    u8 info;

    info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    ASSERT_EQ(info, (STB_GLOBAL << 4) | STT_FUNC);
    ASSERT_EQ(ELF64_ST_BIND(info), STB_GLOBAL);
    ASSERT_EQ(ELF64_ST_TYPE(info), STT_FUNC);

    info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
    ASSERT_EQ(ELF64_ST_BIND(info), STB_LOCAL);
    ASSERT_EQ(ELF64_ST_TYPE(info), STT_SECTION);
}

/* ===== minimal in-memory ELF object ===== */

/*
 * Build a minimal relocatable ELF in a byte buffer:
 *   ELF header
 *   .text section (4 bytes of NOP)
 *   section header table (3 entries: null, .text, .shstrtab)
 *   section name string table
 *
 * Then parse it back and verify structure.
 */
TEST(minimal_elf_object_in_memory)
{
    /*
     * Layout:
     *   [0x000 .. 0x03f]  ELF header (64 bytes)
     *   [0x040 .. 0x043]  .text data  (4 bytes: one NOP instruction)
     *   [0x044 .. 0x058]  .shstrtab data (21 bytes: "\0.text\0.shstrtab\0")
     *   padding to 8-byte alignment at 0x060
     *   [0x060 .. 0x13f]  section headers: 3 entries * 64 bytes = 192 bytes
     */
    u8 buf[0x140];
    Elf64_Ehdr *ehdr;
    Elf64_Shdr *shdrs;
    u8 *text_data;
    char *shstrtab;
    u32 nop;
    u16 shstrtab_size;

    memset(buf, 0, sizeof(buf));

    /* string table: "\0.text\0.shstrtab\0" */
    shstrtab_size = 17;

    /* ELF header */
    ehdr = (Elf64_Ehdr *)buf;
    make_rel_ehdr(ehdr);
    ehdr->e_shoff    = 0x060;
    ehdr->e_shnum    = 3;
    ehdr->e_shstrndx = 2;

    /* .text data at offset 0x40 */
    text_data = buf + 0x040;
    nop = 0xD503201F;  /* AArch64 NOP */
    text_data[0] = (u8)(nop & 0xff);
    text_data[1] = (u8)((nop >> 8) & 0xff);
    text_data[2] = (u8)((nop >> 16) & 0xff);
    text_data[3] = (u8)((nop >> 24) & 0xff);

    /* .shstrtab data at offset 0x44 */
    shstrtab = (char *)(buf + 0x044);
    shstrtab[0] = '\0';
    memcpy(shstrtab + 1, ".text", 6);   /* includes NUL */
    memcpy(shstrtab + 7, ".shstrtab", 10); /* includes NUL */

    /* section headers at offset 0x60 */
    shdrs = (Elf64_Shdr *)(buf + 0x060);

    /* [0] null */
    memset(&shdrs[0], 0, sizeof(Elf64_Shdr));

    /* [1] .text */
    memset(&shdrs[1], 0, sizeof(Elf64_Shdr));
    shdrs[1].sh_name      = 1;  /* ".text" at offset 1 in shstrtab */
    shdrs[1].sh_type      = SHT_PROGBITS;
    shdrs[1].sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[1].sh_offset    = 0x040;
    shdrs[1].sh_size      = 4;
    shdrs[1].sh_addralign = 4;

    /* [2] .shstrtab */
    memset(&shdrs[2], 0, sizeof(Elf64_Shdr));
    shdrs[2].sh_name      = 7;  /* ".shstrtab" at offset 7 in shstrtab */
    shdrs[2].sh_type      = SHT_STRTAB;
    shdrs[2].sh_offset    = 0x044;
    shdrs[2].sh_size      = (u64)shstrtab_size;
    shdrs[2].sh_addralign = 1;

    /* ---- verify the constructed ELF ---- */

    /* magic */
    ASSERT_EQ(buf[0], 0x7f);
    ASSERT_EQ(buf[1], 'E');
    ASSERT_EQ(buf[2], 'L');
    ASSERT_EQ(buf[3], 'F');

    /* header fields */
    ASSERT_EQ(ehdr->e_type, ET_REL);
    ASSERT_EQ(ehdr->e_machine, EM_AARCH64);
    ASSERT_EQ(ehdr->e_shnum, 3);
    ASSERT_EQ(ehdr->e_shstrndx, 2);

    /* section header table location */
    ASSERT_EQ(ehdr->e_shoff, 0x060);

    /* null section */
    ASSERT_EQ(shdrs[0].sh_type, SHT_NULL);

    /* .text section */
    ASSERT_EQ(shdrs[1].sh_type, SHT_PROGBITS);
    ASSERT_EQ(shdrs[1].sh_flags, SHF_ALLOC | SHF_EXECINSTR);
    ASSERT_EQ(shdrs[1].sh_size, 4);

    /* section name lookup via shstrtab */
    ASSERT_STR_EQ(shstrtab + shdrs[1].sh_name, ".text");
    ASSERT_STR_EQ(shstrtab + shdrs[2].sh_name, ".shstrtab");

    /* .text content is a NOP */
    ASSERT_EQ(text_data[0], 0x1f);
    ASSERT_EQ(text_data[1], 0x20);
    ASSERT_EQ(text_data[2], 0x03);
    ASSERT_EQ(text_data[3], 0xd5);
}

/* ===== full object with symtab ===== */

/*
 * Build an in-memory ELF with:
 *   sections: null, .text, .symtab, .strtab, .shstrtab
 *   symbols:  null, _start (global, func, in .text at offset 0)
 *
 * Verify symbol table parsing.
 */
TEST(elf_with_symtab_in_memory)
{
    /*
     * Layout:
     *   0x000  ELF header (64 B)
     *   0x040  .text (4 B)
     *   0x044  .strtab (8 B: "\0_start\0")
     *   0x04c  pad to 8-byte align -> 0x050
     *   0x050  .symtab (2 entries * 24 B = 48 B) -> ends at 0x080
     *   0x080  .shstrtab (33 B: "\0.text\0.symtab\0.strtab\0.shstrtab\0")
     *   pad to 8-byte align -> 0x0a8 (0xa8 is 168, 168%8==0, already aligned)
     *   0x0a8  section headers: 5 entries * 64 B = 320 B -> ends at 0x1e8
     */
    u8 buf[0x200];
    Elf64_Ehdr *ehdr;
    Elf64_Shdr *shdrs;
    Elf64_Sym *syms;
    char *strtab;
    char *shstrtab;
    u32 nop;
    u8 *text_data;
    int strtab_start_name;
    int symtab_start_name;
    int shstrtab_start_name;

    memset(buf, 0, sizeof(buf));

    /* ELF header */
    ehdr = (Elf64_Ehdr *)buf;
    make_rel_ehdr(ehdr);
    ehdr->e_shoff    = 0x0a8;
    ehdr->e_shnum    = 5;
    ehdr->e_shstrndx = 4;

    /* .text at 0x40 */
    text_data = buf + 0x040;
    nop = 0xD503201F;
    text_data[0] = (u8)(nop & 0xff);
    text_data[1] = (u8)((nop >> 8) & 0xff);
    text_data[2] = (u8)((nop >> 16) & 0xff);
    text_data[3] = (u8)((nop >> 24) & 0xff);

    /* .strtab at 0x44: "\0_start\0" */
    strtab = (char *)(buf + 0x044);
    strtab[0] = '\0';
    memcpy(strtab + 1, "_start", 7); /* includes NUL */

    /* .symtab at 0x50: two entries */
    syms = (Elf64_Sym *)(buf + 0x050);

    /* [0] null symbol */
    memset(&syms[0], 0, sizeof(Elf64_Sym));

    /* [1] _start */
    memset(&syms[1], 0, sizeof(Elf64_Sym));
    syms[1].st_name  = 1;  /* "_start" at offset 1 in strtab */
    syms[1].st_info  = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    syms[1].st_shndx = 1;  /* .text section index */
    syms[1].st_value = 0;
    syms[1].st_size  = 4;

    /* .shstrtab at 0x80 */
    /* "\0.text\0.symtab\0.strtab\0.shstrtab\0" */
    shstrtab = (char *)(buf + 0x080);
    shstrtab[0] = '\0';
    memcpy(shstrtab + 1, ".text", 6);       /* offset 1 */
    memcpy(shstrtab + 7, ".symtab", 8);     /* offset 7 */
    memcpy(shstrtab + 15, ".strtab", 8);    /* offset 15 */
    memcpy(shstrtab + 23, ".shstrtab", 10); /* offset 23 */
    /* total: 33 bytes */

    strtab_start_name  = 15;
    symtab_start_name  = 7;
    shstrtab_start_name = 23;

    /* section headers at 0xa8 */
    shdrs = (Elf64_Shdr *)(buf + 0x0a8);

    /* [0] null */
    memset(&shdrs[0], 0, sizeof(Elf64_Shdr));

    /* [1] .text */
    memset(&shdrs[1], 0, sizeof(Elf64_Shdr));
    shdrs[1].sh_name      = 1;
    shdrs[1].sh_type      = SHT_PROGBITS;
    shdrs[1].sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[1].sh_offset    = 0x040;
    shdrs[1].sh_size      = 4;
    shdrs[1].sh_addralign = 4;

    /* [2] .symtab */
    memset(&shdrs[2], 0, sizeof(Elf64_Shdr));
    shdrs[2].sh_name    = (u32)symtab_start_name;
    shdrs[2].sh_type    = SHT_SYMTAB;
    shdrs[2].sh_offset  = 0x050;
    shdrs[2].sh_size    = 2 * sizeof(Elf64_Sym);
    shdrs[2].sh_link    = 3;  /* .strtab section index */
    shdrs[2].sh_info    = 1;  /* first global symbol index */
    shdrs[2].sh_entsize = sizeof(Elf64_Sym);
    shdrs[2].sh_addralign = 8;

    /* [3] .strtab */
    memset(&shdrs[3], 0, sizeof(Elf64_Shdr));
    shdrs[3].sh_name      = (u32)strtab_start_name;
    shdrs[3].sh_type      = SHT_STRTAB;
    shdrs[3].sh_offset    = 0x044;
    shdrs[3].sh_size      = 8;
    shdrs[3].sh_addralign = 1;

    /* [4] .shstrtab */
    memset(&shdrs[4], 0, sizeof(Elf64_Shdr));
    shdrs[4].sh_name      = (u32)shstrtab_start_name;
    shdrs[4].sh_type      = SHT_STRTAB;
    shdrs[4].sh_offset    = 0x080;
    shdrs[4].sh_size      = 33;
    shdrs[4].sh_addralign = 1;

    /* ---- verify ---- */

    /* header */
    ASSERT_EQ(ehdr->e_shnum, 5);
    ASSERT_EQ(ehdr->e_shstrndx, 4);

    /* .text section */
    ASSERT_STR_EQ(shstrtab + shdrs[1].sh_name, ".text");
    ASSERT_EQ(shdrs[1].sh_type, SHT_PROGBITS);

    /* .symtab section */
    ASSERT_STR_EQ(shstrtab + shdrs[2].sh_name, ".symtab");
    ASSERT_EQ(shdrs[2].sh_type, SHT_SYMTAB);
    ASSERT_EQ(shdrs[2].sh_entsize, sizeof(Elf64_Sym));

    /* number of symbols */
    ASSERT_EQ(shdrs[2].sh_size / sizeof(Elf64_Sym), 2);

    /* linked string table */
    ASSERT_EQ(shdrs[2].sh_link, 3);
    ASSERT_STR_EQ(shstrtab + shdrs[3].sh_name, ".strtab");

    /* null symbol */
    ASSERT_EQ(syms[0].st_name, 0);
    ASSERT_EQ(syms[0].st_info, 0);

    /* _start symbol */
    ASSERT_STR_EQ(strtab + syms[1].st_name, "_start");
    ASSERT_EQ(ELF64_ST_BIND(syms[1].st_info), STB_GLOBAL);
    ASSERT_EQ(ELF64_ST_TYPE(syms[1].st_info), STT_FUNC);
    ASSERT_EQ(syms[1].st_shndx, 1);
    ASSERT_EQ(syms[1].st_value, 0);
    ASSERT_EQ(syms[1].st_size, 4);
}

/* ===== section header parsing tests ===== */

/*
 * Parse an array of section headers and verify we can locate
 * sections by type.
 */
TEST(find_section_by_type)
{
    Elf64_Shdr shdrs[4];
    int found;
    int i;

    memset(shdrs, 0, sizeof(shdrs));

    shdrs[0].sh_type = SHT_NULL;
    shdrs[1].sh_type = SHT_PROGBITS;
    shdrs[2].sh_type = SHT_SYMTAB;
    shdrs[3].sh_type = SHT_STRTAB;

    /* search for SHT_SYMTAB */
    found = -1;
    for (i = 0; i < 4; i++) {
        if (shdrs[i].sh_type == SHT_SYMTAB) {
            found = i;
            break;
        }
    }
    ASSERT_EQ(found, 2);
}

TEST(count_alloc_sections)
{
    Elf64_Shdr shdrs[5];
    int count;
    int i;

    memset(shdrs, 0, sizeof(shdrs));

    shdrs[0].sh_type  = SHT_NULL;
    shdrs[0].sh_flags = 0;

    shdrs[1].sh_type  = SHT_PROGBITS;
    shdrs[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;

    shdrs[2].sh_type  = SHT_PROGBITS;
    shdrs[2].sh_flags = SHF_ALLOC | SHF_WRITE;

    shdrs[3].sh_type  = SHT_NOBITS;
    shdrs[3].sh_flags = SHF_ALLOC | SHF_WRITE;

    shdrs[4].sh_type  = SHT_SYMTAB;
    shdrs[4].sh_flags = 0; /* non-allocatable */

    count = 0;
    for (i = 0; i < 5; i++) {
        if (shdrs[i].sh_flags & SHF_ALLOC) {
            count++;
        }
    }
    ASSERT_EQ(count, 3);
}

/* ===== relocation info encoding ===== */

TEST(rela_info_encoding)
{
    u64 info;

    info = ELF64_R_INFO(5, R_AARCH64_CALL26);
    ASSERT_EQ(ELF64_R_SYM(info), 5);
    ASSERT_EQ(ELF64_R_TYPE(info), R_AARCH64_CALL26);
}

TEST(rela_info_roundtrip)
{
    u64 info;
    u32 types[4];
    int i;

    types[0] = R_AARCH64_CALL26;
    types[1] = R_AARCH64_JUMP26;
    types[2] = R_AARCH64_ABS64;
    types[3] = R_AARCH64_ADR_PREL_PG_HI21;

    for (i = 0; i < 4; i++) {
        info = ELF64_R_INFO((u64)i + 1, types[i]);
        ASSERT_EQ(ELF64_R_SYM(info), i + 1);
        ASSERT_EQ(ELF64_R_TYPE(info), types[i]);
    }
}

TEST(rela_entry_fields)
{
    Elf64_Rela rela;

    memset(&rela, 0, sizeof(rela));
    rela.r_offset = 0x10;
    rela.r_info   = ELF64_R_INFO(1, R_AARCH64_CALL26);
    rela.r_addend = 0;

    ASSERT_EQ(rela.r_offset, 0x10);
    ASSERT_EQ(ELF64_R_SYM(rela.r_info), 1);
    ASSERT_EQ(ELF64_R_TYPE(rela.r_info), R_AARCH64_CALL26);
    ASSERT_EQ(rela.r_addend, 0);
}

TEST(rela_negative_addend)
{
    Elf64_Rela rela;

    memset(&rela, 0, sizeof(rela));
    rela.r_addend = -4;

    ASSERT_EQ(rela.r_addend, -4);
}

/* ===== main ===== */

int main(void)
{
    printf("test_elf:\n");

    /* ELF header field tests */
    RUN_TEST(ehdr_magic_bytes);
    RUN_TEST(ehdr_class_and_data);
    RUN_TEST(ehdr_version);
    RUN_TEST(ehdr_osabi);
    RUN_TEST(ehdr_type_rel);
    RUN_TEST(ehdr_type_exec);
    RUN_TEST(ehdr_machine);
    RUN_TEST(ehdr_entry_zero_for_rel);
    RUN_TEST(ehdr_entry_for_exec);
    RUN_TEST(ehdr_sizes);
    RUN_TEST(ehdr_phentsize_exec);
    RUN_TEST(ehdr_padding_zeroed);

    /* raw byte / struct size tests */
    RUN_TEST(ehdr_raw_magic_at_offset_zero);
    RUN_TEST(ehdr_struct_size);
    RUN_TEST(phdr_struct_size);
    RUN_TEST(shdr_struct_size);
    RUN_TEST(sym_struct_size);
    RUN_TEST(rela_struct_size);

    /* section header tests */
    RUN_TEST(shdr_null_section);
    RUN_TEST(shdr_text_section);
    RUN_TEST(shdr_data_section);
    RUN_TEST(shdr_bss_section);
    RUN_TEST(shdr_symtab_section);
    RUN_TEST(shdr_strtab_section);
    RUN_TEST(shdr_rela_section);

    /* symbol table tests */
    RUN_TEST(sym_null_entry);
    RUN_TEST(sym_global_func);
    RUN_TEST(sym_local_notype);
    RUN_TEST(sym_section_symbol);
    RUN_TEST(sym_undefined_reference);
    RUN_TEST(sym_absolute_value);
    RUN_TEST(sym_info_encoding);

    /* in-memory ELF construction */
    RUN_TEST(minimal_elf_object_in_memory);
    RUN_TEST(elf_with_symtab_in_memory);

    /* section parsing */
    RUN_TEST(find_section_by_type);
    RUN_TEST(count_alloc_sections);

    /* relocation info encoding */
    RUN_TEST(rela_info_encoding);
    RUN_TEST(rela_info_roundtrip);
    RUN_TEST(rela_entry_fields);
    RUN_TEST(rela_negative_addend);

    TEST_SUMMARY();
    return tests_failed;
}
