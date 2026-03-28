/*
 * test_dynamic.c - Tests for shared library / dynamic linking support
 * Verifies that the linker produces valid ET_DYN ELF files with
 * the required dynamic sections.
 * Pure C89.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "elf.h"

/* system() is in C89 stdlib but our libc header doesn't declare it */
extern int system(const char *cmd);

/*
 * Create a minimal relocatable object file in memory that contains
 * a single .text section with one global function symbol.
 * Writes it to the given path.
 */
static void write_minimal_obj(const char *path)
{
    FILE *f;
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdrs[5]; /* null, .text, .symtab, .strtab, .shstrtab */
    Elf64_Sym syms[2];    /* null + one function */
    u8 text_data[4];      /* ret instruction */
    int i;

    /* shstrtab: \0 .text\0 .symtab\0 .strtab\0 .shstrtab\0 */
    static const char shstrtab[] =
        "\0.text\0.symtab\0.strtab\0.shstrtab\0";
    u32 shstrtab_sz = sizeof(shstrtab);

    /* strtab: \0 add_nums\0 */
    static const char strtab[] = "\0add_nums\0";
    u32 strtab_sz = sizeof(strtab);

    u64 text_off;
    u64 symtab_off;
    u64 strtab_off;
    u64 shstrtab_off;
    u64 shdrs_off;

    /* text: RET instruction (0xD65F03C0) */
    text_data[0] = 0xC0;
    text_data[1] = 0x03;
    text_data[2] = 0x5F;
    text_data[3] = 0xD6;

    /* Build ELF header */
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
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = 5;
    ehdr.e_shstrndx = 4;

    text_off   = 0x100;
    symtab_off = 0x108;
    strtab_off = symtab_off + 2 * sizeof(Elf64_Sym);
    shstrtab_off = strtab_off + strtab_sz;
    shdrs_off = (shstrtab_off + shstrtab_sz + 7) & ~(u64)7;

    ehdr.e_shoff = shdrs_off;

    /* null section */
    memset(&shdrs[0], 0, sizeof(Elf64_Shdr));

    /* .text */
    memset(&shdrs[1], 0, sizeof(Elf64_Shdr));
    shdrs[1].sh_name = 1;
    shdrs[1].sh_type = SHT_PROGBITS;
    shdrs[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[1].sh_offset = text_off;
    shdrs[1].sh_size = 4;
    shdrs[1].sh_addralign = 4;

    /* .symtab */
    memset(&shdrs[2], 0, sizeof(Elf64_Shdr));
    shdrs[2].sh_name = 7;
    shdrs[2].sh_type = SHT_SYMTAB;
    shdrs[2].sh_offset = symtab_off;
    shdrs[2].sh_size = 2 * sizeof(Elf64_Sym);
    shdrs[2].sh_link = 3;
    shdrs[2].sh_info = 1;
    shdrs[2].sh_entsize = sizeof(Elf64_Sym);
    shdrs[2].sh_addralign = 8;

    /* .strtab */
    memset(&shdrs[3], 0, sizeof(Elf64_Shdr));
    shdrs[3].sh_name = 15;
    shdrs[3].sh_type = SHT_STRTAB;
    shdrs[3].sh_offset = strtab_off;
    shdrs[3].sh_size = strtab_sz;
    shdrs[3].sh_addralign = 1;

    /* .shstrtab */
    memset(&shdrs[4], 0, sizeof(Elf64_Shdr));
    shdrs[4].sh_name = 23;
    shdrs[4].sh_type = SHT_STRTAB;
    shdrs[4].sh_offset = shstrtab_off;
    shdrs[4].sh_size = shstrtab_sz;
    shdrs[4].sh_addralign = 1;

    /* symbols */
    memset(&syms[0], 0, sizeof(Elf64_Sym));

    syms[1].st_name = 1;
    syms[1].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    syms[1].st_other = 0;
    syms[1].st_shndx = 1;
    syms[1].st_value = 0;
    syms[1].st_size = 4;

    /* write to file */
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot create %s\n", path);
        exit(1);
    }

    fwrite(&ehdr, 1, sizeof(ehdr), f);

    for (i = (int)sizeof(ehdr); i < (int)text_off; i++) {
        fputc(0, f);
    }

    fwrite(text_data, 1, 4, f);

    for (i = (int)text_off + 4; i < (int)symtab_off; i++) {
        fputc(0, f);
    }

    fwrite(syms, 1, sizeof(syms), f);
    fwrite(strtab, 1, strtab_sz, f);
    fwrite(shstrtab, 1, shstrtab_sz, f);

    {
        long cur = ftell(f);
        while ((unsigned long)cur < shdrs_off) {
            fputc(0, f);
            cur++;
        }
    }

    fwrite(shdrs, 1, sizeof(shdrs), f);
    fclose(f);
}

/*
 * test_shared_output - verify free-ld -shared produces ET_DYN
 * with required sections.
 */
TEST(shared_output)
{
    const char *obj_path = "/tmp/test_dyn_obj.o";
    const char *so_path = "/tmp/test_dyn.so";
    char cmd[256];
    int ret;
    FILE *f;
    Elf64_Ehdr ehdr;

    write_minimal_obj(obj_path);

    sprintf(cmd, "./free-ld -shared -o %s %s", so_path, obj_path);
    ret = system(cmd);
    ret = (ret >> 8) & 0xff;
    ASSERT_EQ(ret, 0);

    f = fopen(so_path, "rb");
    ASSERT(f != NULL);

    ASSERT_EQ((int)fread(&ehdr, 1, sizeof(ehdr), f),
              (int)sizeof(ehdr));

    /* ET_DYN */
    ASSERT_EQ(ehdr.e_type, ET_DYN);
    ASSERT_EQ(ehdr.e_machine, EM_AARCH64);

    /* at least 2 program headers (LOAD + DYNAMIC) */
    ASSERT(ehdr.e_phnum >= 2);

    /* check for PT_DYNAMIC */
    {
        Elf64_Phdr phdr;
        int found_dynamic = 0;
        int pi;

        fseek(f, (long)ehdr.e_phoff, SEEK_SET);
        for (pi = 0; pi < (int)ehdr.e_phnum; pi++) {
            fread(&phdr, 1, sizeof(phdr), f);
            if (phdr.p_type == PT_DYNAMIC) {
                found_dynamic = 1;
            }
        }
        ASSERT(found_dynamic);
    }

    /* check for dynamic sections */
    {
        Elf64_Shdr *shdrs_out;
        char *shstrtab_buf;
        int si;
        int found_dynsym = 0;
        int found_dynstr = 0;
        int found_hash = 0;
        int found_dynamic_sec = 0;
        int found_got = 0;
        Elf64_Shdr *ssh;

        shdrs_out = (Elf64_Shdr *)calloc(
            (unsigned long)ehdr.e_shnum, sizeof(Elf64_Shdr));

        fseek(f, (long)ehdr.e_shoff, SEEK_SET);
        fread(shdrs_out, sizeof(Elf64_Shdr),
              (unsigned long)ehdr.e_shnum, f);

        ssh = &shdrs_out[ehdr.e_shstrndx];
        shstrtab_buf = (char *)malloc(
            (unsigned long)ssh->sh_size);
        fseek(f, (long)ssh->sh_offset, SEEK_SET);
        fread(shstrtab_buf, 1,
              (unsigned long)ssh->sh_size, f);

        for (si = 0; si < (int)ehdr.e_shnum; si++) {
            const char *name;
            if (shdrs_out[si].sh_name >= ssh->sh_size) {
                continue;
            }
            name = shstrtab_buf + shdrs_out[si].sh_name;
            if (strcmp(name, ".dynsym") == 0) {
                found_dynsym = 1;
                ASSERT_EQ(shdrs_out[si].sh_type,
                          (u32)SHT_DYNSYM);
            }
            if (strcmp(name, ".dynstr") == 0) {
                found_dynstr = 1;
            }
            if (strcmp(name, ".hash") == 0) {
                found_hash = 1;
                ASSERT_EQ(shdrs_out[si].sh_type,
                          (u32)SHT_HASH);
            }
            if (strcmp(name, ".dynamic") == 0) {
                found_dynamic_sec = 1;
                ASSERT_EQ(shdrs_out[si].sh_type,
                          (u32)SHT_DYNAMIC);
            }
            if (strcmp(name, ".got") == 0) {
                found_got = 1;
            }
        }

        free(shstrtab_buf);

        ASSERT(found_dynsym);
        ASSERT(found_dynstr);
        ASSERT(found_hash);
        ASSERT(found_dynamic_sec);
        ASSERT(found_got);

        free(shdrs_out);
    }

    fclose(f);

    remove(obj_path);
    remove(so_path);
}

int main(void)
{
    RUN_TEST(shared_output);
    TEST_SUMMARY();
    return tests_failed > 0 ? 1 : 0;
}
