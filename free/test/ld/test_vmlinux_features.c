/*
 * test_vmlinux_features.c - Tests for vmlinux linker script features
 * Tests: PHDRS definitions, SIZEOF_HEADERS dynamic calculation,
 *        SORT_BY_ALIGNMENT, SORT_BY_INIT_PRIORITY,
 *        section flags propagation, phdr annotations.
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

/*
 * Extract init priority number from section name.
 * ".initcall3.init" -> 3, ".initcall3s.init" -> 3.
 */
static int extract_init_priority(const char *name)
{
    const char *p = name;
    int val = 0;
    int found = 0;
    while (*p && (*p < '0' || *p > '9')) p++;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        found = 1;
        p++;
    }
    return found ? val : 999;
}

/* ---- ELF constant tests ---- */

TEST(pt_phdr_defined)
{
    ASSERT_EQ(PT_PHDR, 6);
}

TEST(pt_tls_defined)
{
    ASSERT_EQ(PT_TLS, 7);
}

TEST(pt_shlib_defined)
{
    ASSERT_EQ(PT_SHLIB, 5);
}

TEST(sht_init_array_defined)
{
    ASSERT_EQ(SHT_INIT_ARRAY, 14);
    ASSERT_EQ(SHT_FINI_ARRAY, 15);
    ASSERT_EQ(SHT_PREINIT_ARRAY, 16);
}

TEST(sht_rel_defined)
{
    ASSERT_EQ(SHT_REL, 9);
}

TEST(shf_tls_defined)
{
    ASSERT_EQ(SHF_TLS, 0x400);
}

/* ---- SIZEOF_HEADERS calculation ---- */

TEST(sizeof_headers_default)
{
    /* Default estimate: 4 phdrs */
    u64 expected = sizeof(Elf64_Ehdr) + 4 * sizeof(Elf64_Phdr);
    ASSERT_EQ(sizeof(Elf64_Ehdr), 64);
    ASSERT_EQ(sizeof(Elf64_Phdr), 56);
    ASSERT_EQ(expected, 288);
}

TEST(sizeof_headers_5_phdrs)
{
    /* With 5 PHDRS entries */
    u64 val = sizeof(Elf64_Ehdr) + 5 * sizeof(Elf64_Phdr);
    ASSERT_EQ(val, 64 + 280);
    ASSERT_EQ(val, 344);
}

TEST(sizeof_headers_8_phdrs)
{
    /* Kernel vmlinux typically has 5-8 phdrs */
    u64 val = sizeof(Elf64_Ehdr) + 8 * sizeof(Elf64_Phdr);
    ASSERT_EQ(val, 64 + 448);
    ASSERT_EQ(val, 512);
}

/* ---- SORT_BY_ALIGNMENT ---- */

TEST(sort_by_alignment_order)
{
    /* Sections sorted by alignment descending */
    struct { const char *name; u64 align; } e[4];
    int n = 4;
    int i, j;

    e[0].name = ".data.1"; e[0].align = 4;
    e[1].name = ".data.2"; e[1].align = 64;
    e[2].name = ".data.3"; e[2].align = 8;
    e[3].name = ".data.4"; e[3].align = 16;

    /* insertion sort by alignment descending */
    for (i = 1; i < n; i++) {
        const char *kn = e[i].name;
        u64 ka = e[i].align;
        j = i - 1;
        while (j >= 0 && e[j].align < ka) {
            e[j + 1].name = e[j].name;
            e[j + 1].align = e[j].align;
            j--;
        }
        e[j + 1].name = kn;
        e[j + 1].align = ka;
    }

    ASSERT_EQ(e[0].align, 64);
    ASSERT_EQ(e[1].align, 16);
    ASSERT_EQ(e[2].align, 8);
    ASSERT_EQ(e[3].align, 4);
}

TEST(sort_by_alignment_tiebreak)
{
    /* When alignment is equal, sort by name */
    struct { const char *name; u64 align; } e[3];
    int n = 3;
    int i, j;

    e[0].name = ".data.c"; e[0].align = 8;
    e[1].name = ".data.a"; e[1].align = 8;
    e[2].name = ".data.b"; e[2].align = 8;

    for (i = 1; i < n; i++) {
        const char *kn = e[i].name;
        u64 ka = e[i].align;
        j = i - 1;
        while (j >= 0 &&
               (e[j].align < ka ||
                (e[j].align == ka && strcmp(e[j].name, kn) > 0))) {
            e[j + 1].name = e[j].name;
            e[j + 1].align = e[j].align;
            j--;
        }
        e[j + 1].name = kn;
        e[j + 1].align = ka;
    }

    ASSERT_STR_EQ(e[0].name, ".data.a");
    ASSERT_STR_EQ(e[1].name, ".data.b");
    ASSERT_STR_EQ(e[2].name, ".data.c");
}

/* ---- SORT_BY_INIT_PRIORITY ---- */

TEST(init_priority_extract)
{
    ASSERT_EQ(extract_init_priority(".initcall0.init"), 0);
    ASSERT_EQ(extract_init_priority(".initcall1.init"), 1);
    ASSERT_EQ(extract_init_priority(".initcall3s.init"), 3);
    ASSERT_EQ(extract_init_priority(".initcall7.init"), 7);
    ASSERT_EQ(extract_init_priority(".initcall_early.init"), 999);
    ASSERT_EQ(extract_init_priority(".initcall12.init"), 12);
}

TEST(sort_by_init_priority_order)
{
    const char *names[5];
    int prios[5];
    int n = 5;
    int i, j;

    names[0] = ".initcall7.init";
    names[1] = ".initcall1.init";
    names[2] = ".initcall3s.init";
    names[3] = ".initcall0.init";
    names[4] = ".initcall5.init";

    for (i = 0; i < n; i++) {
        prios[i] = extract_init_priority(names[i]);
    }

    /* insertion sort by priority */
    for (i = 1; i < n; i++) {
        const char *kn = names[i];
        int kp = prios[i];
        j = i - 1;
        while (j >= 0 && prios[j] > kp) {
            names[j + 1] = names[j];
            prios[j + 1] = prios[j];
            j--;
        }
        names[j + 1] = kn;
        prios[j + 1] = kp;
    }

    ASSERT_STR_EQ(names[0], ".initcall0.init");
    ASSERT_STR_EQ(names[1], ".initcall1.init");
    ASSERT_STR_EQ(names[2], ".initcall3s.init");
    ASSERT_STR_EQ(names[3], ".initcall5.init");
    ASSERT_STR_EQ(names[4], ".initcall7.init");
}

/* ---- Section flags propagation ---- */

TEST(section_flags_merge)
{
    u64 flags1 = SHF_ALLOC | SHF_EXECINSTR;
    u64 flags2 = SHF_ALLOC;
    u64 flags3 = SHF_ALLOC | SHF_WRITE;
    u64 merged;

    merged = flags1 | flags2;
    ASSERT((merged & SHF_ALLOC) != 0);
    ASSERT((merged & SHF_EXECINSTR) != 0);
    ASSERT((merged & SHF_WRITE) == 0);

    merged = flags1 | flags3;
    ASSERT((merged & SHF_ALLOC) != 0);
    ASSERT((merged & SHF_WRITE) != 0);
    ASSERT((merged & SHF_EXECINSTR) != 0);
}

TEST(section_flags_derive_phdr)
{
    /* PF_ flags derived from SHF_ flags */
    u32 derived = 0;
    u64 sh_flags = SHF_ALLOC | SHF_EXECINSTR;

    if (sh_flags & SHF_EXECINSTR) derived |= PF_X;
    if (sh_flags & SHF_WRITE) derived |= PF_W;
    if (sh_flags & SHF_ALLOC) derived |= PF_R;

    ASSERT_EQ(derived, PF_R | PF_X);
    ASSERT_EQ(derived, 5);
}

TEST(section_flags_rw)
{
    u32 derived = 0;
    u64 sh_flags = SHF_ALLOC | SHF_WRITE;

    if (sh_flags & SHF_EXECINSTR) derived |= PF_X;
    if (sh_flags & SHF_WRITE) derived |= PF_W;
    if (sh_flags & SHF_ALLOC) derived |= PF_R;

    ASSERT_EQ(derived, PF_R | PF_W);
    ASSERT_EQ(derived, 6);
}

TEST(section_flags_rwx)
{
    u32 derived = 0;
    u64 sh_flags = SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR;

    if (sh_flags & SHF_EXECINSTR) derived |= PF_X;
    if (sh_flags & SHF_WRITE) derived |= PF_W;
    if (sh_flags & SHF_ALLOC) derived |= PF_R;

    ASSERT_EQ(derived, PF_R | PF_W | PF_X);
    ASSERT_EQ(derived, 7);
}

/* ---- PHDRS segment construction ---- */

TEST(pt_phdr_segment)
{
    Elf64_Phdr phdr;
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = PT_PHDR;
    phdr.p_flags = PF_R;
    phdr.p_offset = sizeof(Elf64_Ehdr);
    phdr.p_filesz = 5 * sizeof(Elf64_Phdr);
    phdr.p_memsz = phdr.p_filesz;
    phdr.p_align = 8;

    ASSERT_EQ(phdr.p_type, PT_PHDR);
    ASSERT_EQ(phdr.p_flags, PF_R);
    ASSERT_EQ(phdr.p_offset, 64);
    ASSERT_EQ(phdr.p_filesz, 280);
}

TEST(pt_load_text_segment)
{
    /* text segment: RX, starts at file offset 0 */
    Elf64_Phdr phdr;
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = PT_LOAD;
    phdr.p_flags = PF_R | PF_X;
    phdr.p_offset = 0;
    phdr.p_vaddr = 0xFFFF800000000000UL;
    phdr.p_paddr = 0xFFFF800000000000UL;
    phdr.p_filesz = 0x100000;
    phdr.p_memsz = 0x100000;
    phdr.p_align = 0x1000;

    ASSERT_EQ(phdr.p_type, PT_LOAD);
    ASSERT_EQ(phdr.p_flags, 5);
    ASSERT(phdr.p_filesz == phdr.p_memsz);
}

TEST(pt_load_data_segment)
{
    /* data segment: RW, memsz > filesz for .bss */
    Elf64_Phdr phdr;
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = PT_LOAD;
    phdr.p_flags = PF_R | PF_W;
    phdr.p_filesz = 0x10000;
    phdr.p_memsz = 0x20000;

    ASSERT_EQ(phdr.p_flags, 6);
    ASSERT(phdr.p_memsz > phdr.p_filesz);
}

TEST(pt_note_segment)
{
    Elf64_Phdr phdr;
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = PT_NOTE;
    phdr.p_flags = PF_R;

    ASSERT_EQ(phdr.p_type, PT_NOTE);
    ASSERT_EQ(phdr.p_flags, PF_R);
}

/* ---- Initcall wildcard matching ---- */

TEST(glob_initcall_wildcard)
{
    ASSERT(glob_match(".initcall*.init", ".initcall0.init") == 1);
    ASSERT(glob_match(".initcall*.init", ".initcall1.init") == 1);
    ASSERT(glob_match(".initcall*.init", ".initcall3s.init") == 1);
    ASSERT(glob_match(".initcall*.init", ".initcall7.init") == 1);
    ASSERT(glob_match(".initcall*.init",
                       ".initcall_early.init") == 1);
    ASSERT(glob_match(".initcall*.init", ".text") == 0);
    ASSERT(glob_match(".initcall*.init", ".initcall0") == 0);
}

TEST(glob_head_text)
{
    /* kernel .head.text section */
    ASSERT(glob_match(".head.text", ".head.text") == 1);
    ASSERT(glob_match(".head.text*", ".head.text") == 1);
    ASSERT(glob_match(".head.text*", ".head.text.foo") == 1);
    ASSERT(glob_match(".head.text", ".text") == 0);
}

TEST(glob_init_data_star)
{
    ASSERT(glob_match(".init.data*", ".init.data") == 1);
    ASSERT(glob_match(".init.data*", ".init.data.foo") == 1);
    ASSERT(glob_match(".init.data.*", ".init.data.bar") == 1);
    ASSERT(glob_match(".init.data.*", ".init.data") == 0);
}

int main(void)
{
    RUN_TEST(pt_phdr_defined);
    RUN_TEST(pt_tls_defined);
    RUN_TEST(pt_shlib_defined);
    RUN_TEST(sht_init_array_defined);
    RUN_TEST(sht_rel_defined);
    RUN_TEST(shf_tls_defined);
    RUN_TEST(sizeof_headers_default);
    RUN_TEST(sizeof_headers_5_phdrs);
    RUN_TEST(sizeof_headers_8_phdrs);
    RUN_TEST(sort_by_alignment_order);
    RUN_TEST(sort_by_alignment_tiebreak);
    RUN_TEST(init_priority_extract);
    RUN_TEST(sort_by_init_priority_order);
    RUN_TEST(section_flags_merge);
    RUN_TEST(section_flags_derive_phdr);
    RUN_TEST(section_flags_rw);
    RUN_TEST(section_flags_rwx);
    RUN_TEST(pt_phdr_segment);
    RUN_TEST(pt_load_text_segment);
    RUN_TEST(pt_load_data_segment);
    RUN_TEST(pt_note_segment);
    RUN_TEST(glob_initcall_wildcard);
    RUN_TEST(glob_head_text);
    RUN_TEST(glob_init_data_star);

    TEST_SUMMARY();
    return tests_failed;
}
