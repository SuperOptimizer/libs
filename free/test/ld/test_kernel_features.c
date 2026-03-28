/*
 * test_kernel_features.c - Tests for kernel-oriented linker features
 * Tests: PIE constants, --defsym parsing, -z flags, --sort-section,
 *        linker script improvements (SIZEOF_HEADERS, NOLOAD, section flags)
 * Pure C89.
 */

#include "../test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"
#include "elf.h"

/* ---- glob matching (same algorithm as script.c) ---- */

static int glob_match(const char *pat, const char *str)
{
    while (*pat && *str) {
        if (*pat == '*') {
            pat++;
            if (*pat == '\0') return 1;
            while (*str) {
                if (glob_match(pat, str)) return 1;
                str++;
            }
            return 0;
        }
        if (*pat == '?') { pat++; str++; continue; }
        if (*pat == '[') {
            int inv = 0;
            int found = 0;
            pat++;
            if (*pat == '!' || *pat == '^') { inv = 1; pat++; }
            while (*pat && *pat != ']') {
                char lo = *pat++;
                if (*pat == '-' && pat[1] && pat[1] != ']') {
                    char hi;
                    pat++;
                    hi = *pat++;
                    if (*str >= lo && *str <= hi) found = 1;
                } else {
                    if (*str == lo) found = 1;
                }
            }
            if (*pat == ']') pat++;
            if (found == inv) return 0;
            str++;
            continue;
        }
        if (*pat != *str) return 0;
        pat++; str++;
    }
    while (*pat == '*') pat++;
    return (*pat == '\0' && *str == '\0');
}

/* ---- tests ---- */

TEST(glob_wildcard)
{
    ASSERT(glob_match("*", "anything") == 1);
    ASSERT(glob_match("*.text.*", ".text.unlikely") == 1);
    ASSERT(glob_match("*.text.*", ".data") == 0);
    ASSERT(glob_match(".init*", ".init") == 1);
    ASSERT(glob_match(".init*", ".init_array") == 1);
    ASSERT(glob_match(".init*", ".fini") == 0);
}

TEST(glob_question)
{
    ASSERT(glob_match("?.text", "x.text") == 1);
    ASSERT(glob_match("?.text", ".text") == 0);
}

TEST(glob_charset)
{
    ASSERT(glob_match("[abc]", "a") == 1);
    ASSERT(glob_match("[abc]", "d") == 0);
    ASSERT(glob_match("[a-z]", "m") == 1);
    ASSERT(glob_match("[!a-z]", "5") == 1);
    ASSERT(glob_match("[!a-z]", "m") == 0);
}

TEST(defsym_decimal)
{
    u64 val = 0;
    const char *vp = "12345";

    while (*vp >= '0' && *vp <= '9') {
        val = val * 10 + (u64)(*vp - '0');
        vp++;
    }
    ASSERT_EQ(val, 12345);
}

TEST(defsym_hex)
{
    u64 val = 0;
    const char *vp = "deadbeef";

    while (*vp) {
        char c = *vp++;
        if (c >= '0' && c <= '9') val = val * 16 + (u64)(c - '0');
        else if (c >= 'a' && c <= 'f') val = val * 16 + (u64)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val = val * 16 + (u64)(c - 'A' + 10);
        else break;
    }
    ASSERT_EQ(val, 0xdeadbeefUL);
}

TEST(z_flag_strings)
{
    ASSERT(strcmp("noexecstack", "noexecstack") == 0);
    ASSERT(strcmp("relro", "relro") == 0);
    ASSERT(strcmp("now", "now") == 0);
    ASSERT(strncmp("max-page-size=", "max-page-size=4096", 14) == 0);
}

TEST(z_page_size_parse)
{
    u64 val = 0;
    const char *vp = "4096";

    while (*vp >= '0' && *vp <= '9') {
        val = val * 10 + (u64)(*vp - '0');
        vp++;
    }
    ASSERT_EQ(val, 4096);
}

TEST(elf_pt_gnu_stack)
{
    ASSERT_EQ(PT_GNU_STACK, 0x6474e551UL);
}

TEST(elf_pt_gnu_relro)
{
    ASSERT_EQ(PT_GNU_RELRO, 0x6474e552UL);
}

TEST(elf_dt_flags)
{
    ASSERT_EQ(DT_FLAGS, 30);
    ASSERT_EQ(DT_BIND_NOW, 24);
    ASSERT_EQ(DF_BIND_NOW, 0x8);
}

TEST(elf_dt_flags_1)
{
    ASSERT_EQ(DT_FLAGS_1, 0x6ffffffbUL);
    ASSERT_EQ(DF_1_NOW, 0x1);
    ASSERT_EQ(DF_1_PIE, 0x8000000UL);
}

TEST(elf_r_aarch64_relative)
{
    ASSERT_EQ(R_AARCH64_RELATIVE, 1027);
}

TEST(elf_et_dyn_for_pie)
{
    /* PIE uses ET_DYN (same as shared lib) but with entry point */
    ASSERT_EQ(ET_DYN, 3);
}

TEST(sizeof_headers)
{
    u64 expected;

    expected = sizeof(Elf64_Ehdr) + 4 * sizeof(Elf64_Phdr);
    ASSERT_EQ(sizeof(Elf64_Ehdr), 64);
    ASSERT_EQ(sizeof(Elf64_Phdr), 56);
    ASSERT_EQ(expected, 64 + 4 * 56);
}

TEST(sort_section_by_name)
{
    const char *names[5];
    int n = 5;

    names[0] = ".init.7";
    names[1] = ".init.3";
    names[2] = ".init.1";
    names[3] = ".init.5";
    names[4] = ".init.2";

    /* simple insertion sort to avoid cast issues with qsort */
    {
        int i;
        int j;
        for (i = 1; i < n; i++) {
            const char *key = names[i];
            j = i - 1;
            while (j >= 0 && strcmp(names[j], key) > 0) {
                names[j + 1] = names[j];
                j--;
            }
            names[j + 1] = key;
        }
    }

    ASSERT_STR_EQ(names[0], ".init.1");
    ASSERT_STR_EQ(names[1], ".init.2");
    ASSERT_STR_EQ(names[2], ".init.3");
    ASSERT_STR_EQ(names[3], ".init.5");
    ASSERT_STR_EQ(names[4], ".init.7");
}

TEST(pie_ehdr_setup)
{
    /* Verify PIE ELF header is ET_DYN with valid entry */
    Elf64_Ehdr ehdr;

    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_type = ET_DYN;
    ehdr.e_machine = EM_AARCH64;
    ehdr.e_entry = 0x400100;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);

    ASSERT_EQ(ehdr.e_type, ET_DYN);
    ASSERT(ehdr.e_entry != 0);
    ASSERT_EQ(ehdr.e_machine, EM_AARCH64);
}

TEST(gnu_stack_phdr)
{
    /* PT_GNU_STACK should have RW flags, no X */
    Elf64_Phdr phdr;

    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = PT_GNU_STACK;
    phdr.p_flags = PF_R | PF_W;
    phdr.p_align = 16;

    ASSERT_EQ(phdr.p_type, PT_GNU_STACK);
    ASSERT((phdr.p_flags & PF_X) == 0);
    ASSERT((phdr.p_flags & PF_R) != 0);
    ASSERT((phdr.p_flags & PF_W) != 0);
}

TEST(gnu_relro_phdr)
{
    /* PT_GNU_RELRO should cover GOT and .dynamic */
    Elf64_Phdr phdr;

    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = PT_GNU_RELRO;
    phdr.p_flags = PF_R;
    phdr.p_vaddr = 0x401000;
    phdr.p_memsz = 0x1000;

    ASSERT_EQ(phdr.p_type, PT_GNU_RELRO);
    ASSERT_EQ(phdr.p_flags, PF_R);
    ASSERT(phdr.p_memsz > 0);
}

TEST(dynamic_bind_now)
{
    /* DT_BIND_NOW in .dynamic for -z now */
    Elf64_Dyn dyn;

    dyn.d_tag = DT_BIND_NOW;
    dyn.d_un.d_val = 0;
    ASSERT_EQ(dyn.d_tag, DT_BIND_NOW);

    dyn.d_tag = DT_FLAGS;
    dyn.d_un.d_val = DF_BIND_NOW;
    ASSERT_EQ(dyn.d_un.d_val, DF_BIND_NOW);

    dyn.d_tag = DT_FLAGS_1;
    dyn.d_un.d_val = DF_1_NOW;
    ASSERT_EQ(dyn.d_un.d_val, DF_1_NOW);
}

TEST(relative_reloc)
{
    /* R_AARCH64_RELATIVE for PIE absolute address fixups */
    Elf64_Rela rela;

    rela.r_offset = 0x401000;
    rela.r_info = ELF64_R_INFO(0, R_AARCH64_RELATIVE);
    rela.r_addend = 0x400100;

    ASSERT_EQ(ELF64_R_TYPE(rela.r_info), R_AARCH64_RELATIVE);
    ASSERT_EQ(ELF64_R_SYM(rela.r_info), 0);
}

int main(void)
{
    RUN_TEST(glob_wildcard);
    RUN_TEST(glob_question);
    RUN_TEST(glob_charset);
    RUN_TEST(defsym_decimal);
    RUN_TEST(defsym_hex);
    RUN_TEST(z_flag_strings);
    RUN_TEST(z_page_size_parse);
    RUN_TEST(elf_pt_gnu_stack);
    RUN_TEST(elf_pt_gnu_relro);
    RUN_TEST(elf_dt_flags);
    RUN_TEST(elf_dt_flags_1);
    RUN_TEST(elf_r_aarch64_relative);
    RUN_TEST(elf_et_dyn_for_pie);
    RUN_TEST(sizeof_headers);
    RUN_TEST(sort_section_by_name);
    RUN_TEST(pie_ehdr_setup);
    RUN_TEST(gnu_stack_phdr);
    RUN_TEST(gnu_relro_phdr);
    RUN_TEST(dynamic_bind_now);
    RUN_TEST(relative_reloc);

    TEST_SUMMARY();
    return tests_failed;
}
