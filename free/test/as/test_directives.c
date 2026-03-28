/*
 * test_directives.c - Tests for new assembler directives.
 * Tests: .inst, .purgem, .req/.unreq, .irp/.irpc, .arch, CFI,
 *        # comment syntax, .section flags.
 * Pure C89. No external dependencies beyond stdio/string.
 */

#include "../test.h"
#include "elf.h"
#include <stdio.h>
#include <string.h>

/* from emit.c */
void assemble(const char *src, const char *outpath);

/* Helper: assemble source, read .text section bytes into buf.
 * Returns number of .text bytes read, or -1 on error. */
static int assemble_and_read_text(const char *src, unsigned char *buf,
                                  int bufsize)
{
    FILE *f;
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    int i;
    int text_idx = -1;
    size_t nread;

    assemble(src, "/tmp/test_dir.o");

    f = fopen("/tmp/test_dir.o", "rb");
    if (!f) return -1;

    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    /* find .text section (type PROGBITS with exec flag) */
    for (i = 0; i < ehdr.e_shnum; i++) {
        fseek(f, (long)(ehdr.e_shoff + (u64)i * ehdr.e_shentsize), SEEK_SET);
        if (fread(&shdr, sizeof(shdr), 1, f) != 1) {
            fclose(f);
            return -1;
        }
        if (shdr.sh_type == SHT_PROGBITS &&
            (shdr.sh_flags & SHF_EXECINSTR)) {
            text_idx = i;
            break;
        }
    }

    if (text_idx < 0) {
        fclose(f);
        return 0; /* no .text section, 0 bytes */
    }

    /* read .text section */
    fseek(f, (long)(ehdr.e_shoff +
                     (u64)text_idx * ehdr.e_shentsize), SEEK_SET);
    if (fread(&shdr, sizeof(shdr), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    nread = (size_t)shdr.sh_size;
    if ((int)nread > bufsize) nread = (size_t)bufsize;

    fseek(f, (long)shdr.sh_offset, SEEK_SET);
    nread = fread(buf, 1, nread, f);
    fclose(f);
    return (int)nread;
}

/* Helper: read a 32-bit LE word from buffer */
static unsigned int read32le(const unsigned char *p)
{
    return (unsigned int)p[0] |
           ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}

/* ===== .inst directive ===== */

TEST(inst_nop)
{
    /* .inst 0xD503201F should emit NOP */
    unsigned char buf[16];
    int n = assemble_and_read_text(
        ".text\n"
        ".inst 0xD503201F\n",
        buf, (int)sizeof(buf));
    ASSERT_EQ(n, 4);
    ASSERT_EQ(read32le(buf), 0xD503201F);
}

TEST(inst_multiple)
{
    /* Multiple .inst values on one line */
    unsigned char buf[16];
    int n = assemble_and_read_text(
        ".text\n"
        ".inst 0xD503201F, 0xD65F03C0\n",
        buf, (int)sizeof(buf));
    ASSERT_EQ(n, 8);
    ASSERT_EQ(read32le(buf), 0xD503201F);
    ASSERT_EQ(read32le(buf + 4), 0xD65F03C0);
}

/* ===== CFI directives (parse & skip) ===== */

TEST(cfi_skip)
{
    /* CFI directives should be accepted and produce no output */
    unsigned char buf[32];
    int n = assemble_and_read_text(
        ".text\n"
        ".cfi_startproc\n"
        "nop\n"
        ".cfi_def_cfa x29, 0\n"
        ".cfi_def_cfa_register x29\n"
        ".cfi_def_cfa_offset 16\n"
        ".cfi_offset x29, -16\n"
        ".cfi_adjust_cfa_offset 8\n"
        ".cfi_restore x29\n"
        ".cfi_remember_state\n"
        ".cfi_restore_state\n"
        ".cfi_endproc\n",
        buf, (int)sizeof(buf));
    /* only the nop should produce code */
    ASSERT_EQ(n, 4);
    ASSERT_EQ(read32le(buf), 0xD503201F);
}

/* ===== .arch directive ===== */

TEST(arch_skip)
{
    /* .arch should be accepted and produce no output */
    unsigned char buf[16];
    int n = assemble_and_read_text(
        ".arch armv8-a\n"
        ".text\n"
        "nop\n",
        buf, (int)sizeof(buf));
    ASSERT_EQ(n, 4);
    ASSERT_EQ(read32le(buf), 0xD503201F);
}

/* ===== .purgem directive ===== */

TEST(purgem_removes_macro)
{
    /* Define a macro, purge it, then use the same name as a label */
    unsigned char buf[16];
    int n = assemble_and_read_text(
        ".text\n"
        ".macro mymacro\n"
        "nop\n"
        ".endm\n"
        "mymacro\n"          /* expands to nop */
        ".purgem mymacro\n"
        "nop\n",             /* plain nop instruction */
        buf, (int)sizeof(buf));
    /* mymacro expansion: 1 nop + plain nop = 2 instructions */
    ASSERT_EQ(n, 8);
    ASSERT_EQ(read32le(buf), 0xD503201F);
    ASSERT_EQ(read32le(buf + 4), 0xD503201F);
}

/* ===== .irp directive ===== */

TEST(irp_basic)
{
    /* .irp should expand body for each value */
    unsigned char buf[32];
    int n = assemble_and_read_text(
        ".text\n"
        ".irp val, 0xD503201F, 0xD65F03C0\n"
        ".inst \\val\n"
        ".endr\n",
        buf, (int)sizeof(buf));
    /* should emit two 4-byte instructions */
    ASSERT_EQ(n, 8);
    ASSERT_EQ(read32le(buf), 0xD503201F);
    ASSERT_EQ(read32le(buf + 4), 0xD65F03C0);
}

/* ===== .irpc directive ===== */

TEST(irpc_basic)
{
    /* .irpc should expand body for each character.
     * Use nop instructions to count iterations. */
    unsigned char buf[32];
    int n = assemble_and_read_text(
        ".text\n"
        ".irpc c, ABC\n"
        "nop\n"
        ".endr\n",
        buf, (int)sizeof(buf));
    /* 3 nop instructions = 12 bytes */
    ASSERT_EQ(n, 12);
    ASSERT_EQ(read32le(buf), 0xD503201F);
    ASSERT_EQ(read32le(buf + 4), 0xD503201F);
    ASSERT_EQ(read32le(buf + 8), 0xD503201F);
}

/* ===== # comment syntax ===== */

TEST(hash_comment)
{
    /* # followed by non-digit should be treated as line comment */
    unsigned char buf[16];
    int n = assemble_and_read_text(
        ".text\n"
        "# this is a comment\n"
        "nop\n",
        buf, (int)sizeof(buf));
    ASSERT_EQ(n, 4);
    ASSERT_EQ(read32le(buf), 0xD503201F);
}

TEST(hash_preprocessor_line)
{
    /* # 1 "file.S" style preprocessor output */
    unsigned char buf[16];
    int n = assemble_and_read_text(
        "# 1 \"test.S\"\n"
        ".text\n"
        "nop\n",
        buf, (int)sizeof(buf));
    ASSERT_EQ(n, 4);
    ASSERT_EQ(read32le(buf), 0xD503201F);
}

/* ===== .section with flags ===== */

TEST(section_flags)
{
    /* .section with full flag syntax should be accepted */
    unsigned char buf[16];
    int n = assemble_and_read_text(
        ".section .text, \"ax\", @progbits\n"
        "nop\n",
        buf, (int)sizeof(buf));
    ASSERT_EQ(n, 4);
    ASSERT_EQ(read32le(buf), 0xD503201F);
}

TEST(section_note_gnustack)
{
    /* .section .note.GNU-stack should not crash */
    unsigned char buf[16];
    int n = assemble_and_read_text(
        ".text\n"
        "nop\n"
        ".section .note.GNU-stack, \"\", @progbits\n",
        buf, (int)sizeof(buf));
    ASSERT_EQ(n, 4);
}

/* ===== Register aliases (.req / .unreq) ===== */

TEST(req_basic)
{
    /* Register alias should work in instructions */
    unsigned char buf[16];
    unsigned int movz_expected;
    int n;
    /* mov x0, #42 encoded as movz x0, #42 = 0xD2800540 */
    /* Using alias: base .req x0; movz base, #42 */
    n = assemble_and_read_text(
        ".text\n"
        "base .req x0\n"
        "movz base, #42\n",
        buf, (int)sizeof(buf));
    ASSERT_EQ(n, 4);
    movz_expected = 0xD2800540;
    ASSERT_EQ(read32le(buf), movz_expected);
}

/* ===== Dynamic named sections ===== */

/* Helper: assemble source and look for a section by name in the ELF.
 * Returns section index (>= 0) on found, -1 on not found. */
static int find_section_by_name(const char *src, const char *sec_name)
{
    FILE *f;
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    Elf64_Shdr shstrtab_shdr;
    char shstrtab_buf[4096];
    int i;

    assemble(src, "/tmp/test_dynsec.o");

    f = fopen("/tmp/test_dynsec.o", "rb");
    if (!f) return -1;

    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    /* read shstrtab section header */
    fseek(f, (long)(ehdr.e_shoff +
                     (u64)ehdr.e_shstrndx * ehdr.e_shentsize), SEEK_SET);
    if (fread(&shstrtab_shdr, sizeof(shstrtab_shdr), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    /* read shstrtab contents */
    if (shstrtab_shdr.sh_size > sizeof(shstrtab_buf)) {
        fclose(f);
        return -1;
    }
    fseek(f, (long)shstrtab_shdr.sh_offset, SEEK_SET);
    memset(shstrtab_buf, 0, sizeof(shstrtab_buf));
    if (fread(shstrtab_buf, 1, (size_t)shstrtab_shdr.sh_size, f)
        != (size_t)shstrtab_shdr.sh_size) {
        fclose(f);
        return -1;
    }

    /* search all sections for matching name */
    for (i = 0; i < ehdr.e_shnum; i++) {
        fseek(f, (long)(ehdr.e_shoff + (u64)i * ehdr.e_shentsize),
              SEEK_SET);
        if (fread(&shdr, sizeof(shdr), 1, f) != 1) continue;
        if (shdr.sh_name < shstrtab_shdr.sh_size &&
            strcmp(shstrtab_buf + shdr.sh_name, sec_name) == 0) {
            fclose(f);
            return i;
        }
    }

    fclose(f);
    return -1;
}

/* Helper: assemble source and read a section's data by name.
 * Returns bytes read, or -1 on error. */
static int read_section_data(const char *src, const char *sec_name,
                             unsigned char *buf, int bufsize,
                             unsigned int *out_type, unsigned long *out_flags)
{
    FILE *f;
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    Elf64_Shdr shstrtab_shdr;
    char shstrtab_buf[4096];
    int i;
    int found = -1;
    size_t nread;

    assemble(src, "/tmp/test_dynsec.o");

    f = fopen("/tmp/test_dynsec.o", "rb");
    if (!f) return -1;

    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    fseek(f, (long)(ehdr.e_shoff +
                     (u64)ehdr.e_shstrndx * ehdr.e_shentsize), SEEK_SET);
    if (fread(&shstrtab_shdr, sizeof(shstrtab_shdr), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    if (shstrtab_shdr.sh_size > sizeof(shstrtab_buf)) {
        fclose(f);
        return -1;
    }
    fseek(f, (long)shstrtab_shdr.sh_offset, SEEK_SET);
    memset(shstrtab_buf, 0, sizeof(shstrtab_buf));
    if (fread(shstrtab_buf, 1, (size_t)shstrtab_shdr.sh_size, f)
        != (size_t)shstrtab_shdr.sh_size) {
        fclose(f);
        return -1;
    }

    for (i = 0; i < ehdr.e_shnum; i++) {
        fseek(f, (long)(ehdr.e_shoff + (u64)i * ehdr.e_shentsize),
              SEEK_SET);
        if (fread(&shdr, sizeof(shdr), 1, f) != 1) continue;
        if (shdr.sh_name < shstrtab_shdr.sh_size &&
            strcmp(shstrtab_buf + shdr.sh_name, sec_name) == 0) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        fclose(f);
        return -1;
    }

    if (out_type) *out_type = shdr.sh_type;
    if (out_flags) *out_flags = shdr.sh_flags;

    nread = (size_t)shdr.sh_size;
    if ((int)nread > bufsize) nread = (size_t)bufsize;

    if (shdr.sh_type == SHT_NOBITS) {
        fclose(f);
        return (int)shdr.sh_size;
    }

    fseek(f, (long)shdr.sh_offset, SEEK_SET);
    nread = fread(buf, 1, nread, f);
    fclose(f);
    return (int)nread;
}

TEST(dynamic_section_init_text)
{
    /* .section .init.text creates a distinct section */
    int idx = find_section_by_name(
        ".section .init.text, \"ax\"\n"
        "nop\n",
        ".init.text");
    ASSERT(idx >= 0);
}

TEST(dynamic_section_ex_table)
{
    /* Kernel-style __ex_table section */
    int idx = find_section_by_name(
        ".section __ex_table, \"a\", @progbits\n"
        ".word 0, 0\n",
        "__ex_table");
    ASSERT(idx >= 0);
}

TEST(dynamic_section_flags)
{
    /* Verify flags are correctly parsed */
    unsigned char buf[32];
    unsigned int stype = 0;
    unsigned long sflags = 0;
    int n = read_section_data(
        ".section .init.data, \"aw\"\n"
        ".word 42\n",
        ".init.data", buf, (int)sizeof(buf), &stype, &sflags);
    ASSERT(n > 0);
    ASSERT_EQ(stype, SHT_PROGBITS);
    ASSERT(sflags & SHF_ALLOC);
    ASSERT(sflags & SHF_WRITE);
}

TEST(dynamic_section_note)
{
    /* %note section type */
    unsigned char buf[32];
    unsigned int stype = 0;
    unsigned long sflags = 0;
    read_section_data(
        ".section .note.gnu.build-id, \"a\", %note\n"
        ".byte 0\n",
        ".note.gnu.build-id", buf, (int)sizeof(buf), &stype, &sflags);
    ASSERT_EQ(stype, SHT_NOTE);
}

TEST(dynamic_section_content)
{
    /* Data emitted to custom section is readable */
    unsigned char buf[32];
    int n = read_section_data(
        ".section .mydata, \"aw\", @progbits\n"
        ".byte 0xAA, 0xBB, 0xCC\n",
        ".mydata", buf, (int)sizeof(buf), NULL, NULL);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(buf[0], 0xAA);
    ASSERT_EQ(buf[1], 0xBB);
    ASSERT_EQ(buf[2], 0xCC);
}

TEST(pushsection_popsection_dynamic)
{
    /* .pushsection creates a named section, .popsection returns */
    unsigned char buf[32];
    int n;

    n = assemble_and_read_text(
        ".text\n"
        "nop\n"
        ".pushsection .init.text, \"ax\"\n"
        "nop\n"
        ".popsection\n"
        "nop\n",
        buf, (int)sizeof(buf));
    /* .text should have 2 nops (8 bytes) */
    ASSERT_EQ(n, 8);

    /* .init.text should also exist and have 1 nop */
    {
        int idx = find_section_by_name(
            ".text\n"
            "nop\n"
            ".pushsection .init.text, \"ax\"\n"
            "nop\n"
            ".popsection\n"
            "nop\n",
            ".init.text");
        ASSERT(idx >= 0);
    }
}

TEST(section_switch_preserves_text)
{
    /* Switching to custom section and back preserves .text */
    unsigned char buf[32];
    int n = assemble_and_read_text(
        ".text\n"
        "nop\n"
        ".section .init.text, \"ax\"\n"
        "nop\n"
        ".text\n"
        "nop\n",
        buf, (int)sizeof(buf));
    /* .text should have 2 nops */
    ASSERT_EQ(n, 8);
}

TEST(incbin_missing_file)
{
    /* .incbin with nonexistent file should not crash */
    unsigned char buf[16];
    int n = assemble_and_read_text(
        ".text\n"
        "nop\n"
        ".data\n"
        ".incbin \"/tmp/nonexistent_file_12345.bin\"\n",
        buf, (int)sizeof(buf));
    ASSERT_EQ(n, 4);
}

/* ===== main ===== */

int main(void)
{
    printf("test_directives:\n");

    RUN_TEST(inst_nop);
    RUN_TEST(inst_multiple);
    RUN_TEST(cfi_skip);
    RUN_TEST(arch_skip);
    RUN_TEST(purgem_removes_macro);
    RUN_TEST(irp_basic);
    RUN_TEST(irpc_basic);
    RUN_TEST(hash_comment);
    RUN_TEST(hash_preprocessor_line);
    RUN_TEST(section_flags);
    RUN_TEST(section_note_gnustack);
    RUN_TEST(req_basic);

    /* Dynamic named sections */
    RUN_TEST(dynamic_section_init_text);
    RUN_TEST(dynamic_section_ex_table);
    RUN_TEST(dynamic_section_flags);
    RUN_TEST(dynamic_section_note);
    RUN_TEST(dynamic_section_content);
    RUN_TEST(pushsection_popsection_dynamic);
    RUN_TEST(section_switch_preserves_text);
    RUN_TEST(incbin_missing_file);

    TEST_SUMMARY();
    return tests_failed;
}
