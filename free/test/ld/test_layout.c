/*
 * test_layout.c - Tests for linker layout: section merging, virtual address
 * assignment, alignment, program header creation, and entry point resolution.
 * Pure C89.
 */

#include "../test.h"
#include <string.h>
#include <stdlib.h>

#include "free.h"
#include "elf.h"

/* ===== section descriptor for layout simulation ===== */

/*
 * Simplified input section: name, type, flags, alignment, size, data.
 * Mirrors what the linker reads from each .o file.
 */
struct input_section {
    const char *name;
    u32  type;
    u64  flags;
    u64  align;
    u64  size;
    u8  *data;
    int  obj_idx;      /* which object file this came from */
    u64  out_addr;     /* assigned virtual address after layout */
    u64  out_offset;   /* assigned file offset after layout */
};

/*
 * Merged output section: multiple input sections with the same name
 * are combined into one contiguous output section.
 */
struct merged_sec {
    const char *name;
    u32  type;
    u64  flags;
    u64  align;
    u64  vaddr;
    u64  offset;
    u64  size;
};

/*
 * Simple symbol for entry point tests.
 */
struct layout_sym {
    const char *name;
    int  section_idx;  /* which input section */
    u64  value;        /* offset within that section */
    u64  resolved;     /* final VA after layout */
    int  is_global;
};

/* ===== alignment helper ===== */

static u64 align_up(u64 val, u64 align)
{
    if (align == 0) {
        return val;
    }
    return (val + align - 1) & ~(align - 1);
}

/* ===== layout simulator ===== */

/*
 * merge_sections: given an array of input sections, merge those with
 * matching names into output merged sections.  Assigns virtual addresses
 * starting at base_vaddr and file offsets starting at base_offset.
 *
 * Returns the number of merged sections produced.
 * Fills each input section's out_addr and out_offset.
 */
static int merge_sections(struct input_section *in, int num_in,
                           struct merged_sec *out, int max_out,
                           u64 base_vaddr, u64 base_offset)
{
    int num_out;
    u64 cur_vaddr;
    u64 cur_offset;
    int i;
    int j;

    num_out    = 0;
    cur_vaddr  = base_vaddr;
    cur_offset = base_offset;

    for (i = 0; i < num_in; i++) {
        int found;

        /* skip non-allocatable sections */
        if (!(in[i].flags & SHF_ALLOC)) {
            continue;
        }

        /* see if we already have a merged section with this name */
        found = -1;
        for (j = 0; j < num_out; j++) {
            if (strcmp(out[j].name, in[i].name) == 0) {
                found = j;
                break;
            }
        }

        if (found < 0) {
            /* create a new merged section */
            if (num_out >= max_out) {
                break;
            }
            found = num_out++;
            out[found].name   = in[i].name;
            out[found].type   = in[i].type;
            out[found].flags  = in[i].flags;
            out[found].align  = in[i].align;
            cur_vaddr  = align_up(cur_vaddr, in[i].align);
            cur_offset = align_up(cur_offset, in[i].align);
            out[found].vaddr  = cur_vaddr;
            out[found].offset = cur_offset;
            out[found].size   = 0;
        } else {
            /* existing merged section: align cursor to this contribution */
            if (in[i].align > out[found].align) {
                out[found].align = in[i].align;
            }
            cur_vaddr  = align_up(out[found].vaddr + out[found].size,
                                   in[i].align);
            cur_offset = align_up(out[found].offset + out[found].size,
                                   in[i].align);
        }

        /* assign VA to this input section */
        in[i].out_addr   = cur_vaddr;
        in[i].out_offset = cur_offset;

        /* grow merged section */
        out[found].size = (cur_vaddr + in[i].size) - out[found].vaddr;

        cur_vaddr  += in[i].size;
        cur_offset += in[i].size;
    }

    return num_out;
}

/* ===== section merging tests ===== */

TEST(merge_single_text)
{
    struct input_section in[1];
    struct merged_sec out[4];
    int n;

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;
    in[0].obj_idx = 0;

    n = merge_sections(in, 1, out, 4, 0x400000, 0x1000);
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(out[0].name, ".text");
    ASSERT_EQ(out[0].size, 0x100);
    ASSERT_EQ(out[0].vaddr, 0x400000);
}

TEST(merge_two_text_sections)
{
    /*
     * Two .text sections from different objects should be merged.
     * obj0: .text 0x100 bytes, align 4
     * obj1: .text 0x80 bytes, align 4
     * total merged .text size = 0x180
     */
    struct input_section in[2];
    struct merged_sec out[4];
    int n;

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;
    in[0].obj_idx = 0;

    in[1].name  = ".text";
    in[1].type  = SHT_PROGBITS;
    in[1].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[1].align = 4;
    in[1].size  = 0x80;
    in[1].obj_idx = 1;

    n = merge_sections(in, 2, out, 4, 0x400000, 0x1000);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(out[0].size, 0x180);
    ASSERT_EQ(in[0].out_addr, 0x400000);
    ASSERT_EQ(in[1].out_addr, 0x400100);
}

TEST(merge_text_and_data)
{
    /*
     * .text and .data should produce two separate merged sections.
     */
    struct input_section in[2];
    struct merged_sec out[4];
    int n;

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;

    in[1].name  = ".data";
    in[1].type  = SHT_PROGBITS;
    in[1].flags = SHF_ALLOC | SHF_WRITE;
    in[1].align = 8;
    in[1].size  = 0x40;

    n = merge_sections(in, 2, out, 4, 0x400000, 0x1000);
    ASSERT_EQ(n, 2);
    ASSERT_STR_EQ(out[0].name, ".text");
    ASSERT_STR_EQ(out[1].name, ".data");
}

TEST(merge_three_objects)
{
    /*
     * Three objects, each with .text:
     *   obj0: 0x100 bytes
     *   obj1: 0x80  bytes
     *   obj2: 0x40  bytes
     * All aligned to 4.  Total size = 0x1C0.
     */
    struct input_section in[3];
    struct merged_sec out[4];
    int n;

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text"; in[0].type = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4; in[0].size = 0x100; in[0].obj_idx = 0;

    in[1].name  = ".text"; in[1].type = SHT_PROGBITS;
    in[1].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[1].align = 4; in[1].size = 0x80; in[1].obj_idx = 1;

    in[2].name  = ".text"; in[2].type = SHT_PROGBITS;
    in[2].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[2].align = 4; in[2].size = 0x40; in[2].obj_idx = 2;

    n = merge_sections(in, 3, out, 4, 0x400000, 0x1000);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(out[0].size, 0x1C0);
    ASSERT_EQ(in[0].out_addr, 0x400000);
    ASSERT_EQ(in[1].out_addr, 0x400100);
    ASSERT_EQ(in[2].out_addr, 0x400180);
}

TEST(merge_skips_non_alloc)
{
    /*
     * Non-allocatable sections (like .symtab) should not appear
     * in the merged output.
     */
    struct input_section in[2];
    struct merged_sec out[4];
    int n;

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;

    in[1].name  = ".symtab";
    in[1].type  = SHT_SYMTAB;
    in[1].flags = 0;  /* not allocatable */
    in[1].align = 8;
    in[1].size  = 0x60;

    n = merge_sections(in, 2, out, 4, 0x400000, 0x1000);
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(out[0].name, ".text");
}

/* ===== virtual address assignment tests ===== */

TEST(vaddr_starts_at_base)
{
    struct input_section in[1];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;

    merge_sections(in, 1, out, 4, 0x400000, 0x1000);
    ASSERT_EQ(out[0].vaddr, 0x400000);
    ASSERT_EQ(in[0].out_addr, 0x400000);
}

TEST(vaddr_sequential)
{
    /*
     * .text at 0x400000 (size 0x100), .data follows.
     * With align=8, .data starts at 0x400100 (already aligned).
     */
    struct input_section in[2];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;

    in[1].name  = ".data";
    in[1].type  = SHT_PROGBITS;
    in[1].flags = SHF_ALLOC | SHF_WRITE;
    in[1].align = 8;
    in[1].size  = 0x40;

    merge_sections(in, 2, out, 4, 0x400000, 0x1000);
    ASSERT_EQ(out[0].vaddr, 0x400000);
    ASSERT(out[1].vaddr >= 0x400100);
}

TEST(vaddr_with_bss)
{
    /*
     * .bss (SHT_NOBITS) gets a virtual address but occupies no file space.
     */
    struct input_section in[2];
    struct merged_sec out[4];
    int n;

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".data";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_WRITE;
    in[0].align = 8;
    in[0].size  = 0x40;

    in[1].name  = ".bss";
    in[1].type  = SHT_NOBITS;
    in[1].flags = SHF_ALLOC | SHF_WRITE;
    in[1].align = 8;
    in[1].size  = 0x200;

    n = merge_sections(in, 2, out, 4, 0x400000, 0x1000);
    ASSERT_EQ(n, 2);
    /* .bss has a virtual address */
    ASSERT(out[1].vaddr > 0);
    ASSERT_EQ(out[1].size, 0x200);
}

/* ===== alignment tests ===== */

TEST(align_up_basic)
{
    ASSERT_EQ(align_up(0, 4), 0);
    ASSERT_EQ(align_up(1, 4), 4);
    ASSERT_EQ(align_up(4, 4), 4);
    ASSERT_EQ(align_up(5, 4), 8);
    ASSERT_EQ(align_up(7, 8), 8);
    ASSERT_EQ(align_up(8, 8), 8);
    ASSERT_EQ(align_up(9, 8), 16);
}

TEST(align_up_page)
{
    ASSERT_EQ(align_up(0x1001, 0x1000), 0x2000);
    ASSERT_EQ(align_up(0x2000, 0x1000), 0x2000);
    ASSERT_EQ(align_up(0x1, 0x1000), 0x1000);
}

TEST(align_up_zero_align)
{
    /* align=0 should be a no-op (pass through) */
    ASSERT_EQ(align_up(42, 0), 42);
}

TEST(section_alignment_respected)
{
    /*
     * First section: .text, size=0x101 (not 8-aligned end).
     * Second section: .data, align=8.
     * .data's start must be 8-byte aligned.
     */
    struct input_section in[2];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x101;

    in[1].name  = ".data";
    in[1].type  = SHT_PROGBITS;
    in[1].flags = SHF_ALLOC | SHF_WRITE;
    in[1].align = 8;
    in[1].size  = 0x40;

    merge_sections(in, 2, out, 4, 0x400000, 0x1000);

    /* .data vaddr must be 8-byte aligned */
    ASSERT_EQ(out[1].vaddr % 8, 0);
    /* .data must follow .text */
    ASSERT(out[1].vaddr >= out[0].vaddr + out[0].size);
}

TEST(merge_alignment_promotion)
{
    /*
     * When merging two .text inputs with different alignments,
     * the merged section should adopt the larger alignment.
     */
    struct input_section in[2];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;

    in[1].name  = ".text";
    in[1].type  = SHT_PROGBITS;
    in[1].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[1].align = 16;
    in[1].size  = 0x80;

    merge_sections(in, 2, out, 4, 0x400000, 0x1000);
    ASSERT(out[0].align >= 16);
    /* second contribution must be 16-byte aligned */
    ASSERT_EQ(in[1].out_addr % 16, 0);
}

/* ===== program header tests ===== */

TEST(phdr_text_segment)
{
    /*
     * A read+execute PT_LOAD segment for .text.
     */
    Elf64_Phdr ph;

    memset(&ph, 0, sizeof(ph));
    ph.p_type   = PT_LOAD;
    ph.p_flags  = PF_R | PF_X;
    ph.p_offset = 0x1000;
    ph.p_vaddr  = 0x400000;
    ph.p_paddr  = 0x400000;
    ph.p_filesz = 0x200;
    ph.p_memsz  = 0x200;
    ph.p_align  = 0x1000;

    ASSERT_EQ(ph.p_type, PT_LOAD);
    ASSERT_EQ(ph.p_flags, PF_R | PF_X);
    ASSERT_EQ(ph.p_vaddr, 0x400000);
    ASSERT_EQ(ph.p_filesz, 0x200);
    ASSERT_EQ(ph.p_memsz, 0x200);
    ASSERT_EQ(ph.p_align, 0x1000);
}

TEST(phdr_data_segment)
{
    /*
     * A read+write PT_LOAD segment for .data + .bss.
     * .bss makes memsz > filesz.
     */
    Elf64_Phdr ph;
    u64 data_size;
    u64 bss_size;

    data_size = 0x40;
    bss_size  = 0x200;

    memset(&ph, 0, sizeof(ph));
    ph.p_type   = PT_LOAD;
    ph.p_flags  = PF_R | PF_W;
    ph.p_offset = 0x2000;
    ph.p_vaddr  = 0x410000;
    ph.p_paddr  = 0x410000;
    ph.p_filesz = data_size;
    ph.p_memsz  = data_size + bss_size;
    ph.p_align  = 0x1000;

    ASSERT_EQ(ph.p_type, PT_LOAD);
    ASSERT_EQ(ph.p_flags, PF_R | PF_W);
    ASSERT_EQ(ph.p_filesz, 0x40);
    ASSERT_EQ(ph.p_memsz, 0x240);
    ASSERT(ph.p_memsz > ph.p_filesz); /* .bss portion */
}

TEST(two_pt_load_segments)
{
    /*
     * Standard layout: two PT_LOAD segments.
     *   [0] text segment: R+X
     *   [1] data segment: R+W
     * They must not overlap in virtual address space.
     */
    Elf64_Phdr phdrs[2];

    memset(phdrs, 0, sizeof(phdrs));

    /* text segment */
    phdrs[0].p_type   = PT_LOAD;
    phdrs[0].p_flags  = PF_R | PF_X;
    phdrs[0].p_vaddr  = 0x400000;
    phdrs[0].p_memsz  = 0x1000;
    phdrs[0].p_filesz = 0x1000;
    phdrs[0].p_align  = 0x1000;

    /* data segment */
    phdrs[1].p_type   = PT_LOAD;
    phdrs[1].p_flags  = PF_R | PF_W;
    phdrs[1].p_vaddr  = 0x410000;
    phdrs[1].p_memsz  = 0x2000;
    phdrs[1].p_filesz = 0x500;
    phdrs[1].p_align  = 0x1000;

    /* both are PT_LOAD */
    ASSERT_EQ(phdrs[0].p_type, PT_LOAD);
    ASSERT_EQ(phdrs[1].p_type, PT_LOAD);

    /* permissions differ */
    ASSERT(phdrs[0].p_flags & PF_X);
    ASSERT(!(phdrs[1].p_flags & PF_X));
    ASSERT(phdrs[1].p_flags & PF_W);
    ASSERT(!(phdrs[0].p_flags & PF_W));

    /* no overlap */
    ASSERT(phdrs[1].p_vaddr >= phdrs[0].p_vaddr + phdrs[0].p_memsz);

    /* alignment satisfied */
    ASSERT_EQ(phdrs[0].p_vaddr % phdrs[0].p_align, 0);
    ASSERT_EQ(phdrs[1].p_vaddr % phdrs[1].p_align, 0);
}

TEST(phdr_null_type)
{
    Elf64_Phdr ph;

    memset(&ph, 0, sizeof(ph));
    ph.p_type = PT_NULL;
    ASSERT_EQ(ph.p_type, PT_NULL);
    ASSERT_EQ(ph.p_filesz, 0);
    ASSERT_EQ(ph.p_memsz, 0);
}

TEST(ehdr_phdr_fields_for_exec)
{
    /*
     * When creating an executable ELF, the header must reference
     * the program header table.
     */
    Elf64_Ehdr ehdr;
    int num_phdrs;

    num_phdrs = 2;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_type      = ET_EXEC;
    ehdr.e_machine   = EM_AARCH64;
    ehdr.e_version   = EV_CURRENT;
    ehdr.e_entry     = 0x400080;
    ehdr.e_phoff     = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = (u16)sizeof(Elf64_Phdr);
    ehdr.e_phnum     = (u16)num_phdrs;
    ehdr.e_ehsize    = (u16)sizeof(Elf64_Ehdr);

    ASSERT_EQ(ehdr.e_type, ET_EXEC);
    ASSERT_EQ(ehdr.e_phoff, 64);
    ASSERT_EQ(ehdr.e_phentsize, sizeof(Elf64_Phdr));
    ASSERT_EQ(ehdr.e_phnum, 2);
    ASSERT_EQ(ehdr.e_entry, 0x400080);
}

/* ===== entry point resolution tests ===== */

TEST(entry_point_from_start_symbol)
{
    /*
     * _start is defined in .text at section offset 0.
     * After layout, .text is at VA 0x400000.
     * _start's resolved address = 0x400000 + 0 = 0x400000.
     */
    struct layout_sym sym;
    struct input_section in[1];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));
    memset(&sym, 0, sizeof(sym));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;

    merge_sections(in, 1, out, 4, 0x400000, 0x1000);

    sym.name        = "_start";
    sym.section_idx = 0;
    sym.value       = 0;
    sym.is_global   = 1;
    sym.resolved    = in[sym.section_idx].out_addr + sym.value;

    ASSERT_EQ(sym.resolved, 0x400000);
}

TEST(entry_point_with_offset)
{
    /*
     * _start at section offset 0x20 within .text.
     * .text base VA = 0x400000.
     * _start resolved = 0x400020.
     */
    struct layout_sym sym;
    struct input_section in[1];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));
    memset(&sym, 0, sizeof(sym));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;

    merge_sections(in, 1, out, 4, 0x400000, 0x1000);

    sym.name        = "_start";
    sym.section_idx = 0;
    sym.value       = 0x20;
    sym.is_global   = 1;
    sym.resolved    = in[sym.section_idx].out_addr + sym.value;

    ASSERT_EQ(sym.resolved, 0x400020);
}

TEST(entry_point_in_second_object)
{
    /*
     * _start is defined in the second object's .text.
     * obj0 .text: 0x100 bytes
     * obj1 .text: 0x80 bytes, _start at offset 0
     *
     * After merge: obj1 .text starts at 0x400100.
     * _start = 0x400100.
     */
    struct layout_sym sym;
    struct input_section in[2];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));
    memset(&sym, 0, sizeof(sym));

    in[0].name  = ".text"; in[0].type = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4; in[0].size = 0x100; in[0].obj_idx = 0;

    in[1].name  = ".text"; in[1].type = SHT_PROGBITS;
    in[1].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[1].align = 4; in[1].size = 0x80; in[1].obj_idx = 1;

    merge_sections(in, 2, out, 4, 0x400000, 0x1000);

    sym.name        = "_start";
    sym.section_idx = 1; /* in[1] */
    sym.value       = 0;
    sym.is_global   = 1;
    sym.resolved    = in[sym.section_idx].out_addr + sym.value;

    ASSERT_EQ(sym.resolved, 0x400100);
}

TEST(multiple_symbols_resolve)
{
    /*
     * Two symbols in the same .text section:
     *   _start at offset 0
     *   helper at offset 0x40
     */
    struct layout_sym syms[2];
    struct input_section in[1];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));
    memset(syms, 0, sizeof(syms));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;

    merge_sections(in, 1, out, 4, 0x400000, 0x1000);

    syms[0].name        = "_start";
    syms[0].section_idx = 0;
    syms[0].value       = 0;
    syms[0].resolved    = in[0].out_addr + syms[0].value;

    syms[1].name        = "helper";
    syms[1].section_idx = 0;
    syms[1].value       = 0x40;
    syms[1].resolved    = in[0].out_addr + syms[1].value;

    ASSERT_EQ(syms[0].resolved, 0x400000);
    ASSERT_EQ(syms[1].resolved, 0x400040);
}

TEST(symbol_in_data_section)
{
    /*
     * A global variable "buffer" at offset 0x10 in .data.
     */
    struct layout_sym sym;
    struct input_section in[2];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));
    memset(&sym, 0, sizeof(sym));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;

    in[1].name  = ".data";
    in[1].type  = SHT_PROGBITS;
    in[1].flags = SHF_ALLOC | SHF_WRITE;
    in[1].align = 8;
    in[1].size  = 0x40;

    merge_sections(in, 2, out, 4, 0x400000, 0x1000);

    sym.name        = "buffer";
    sym.section_idx = 1;
    sym.value       = 0x10;
    sym.is_global   = 1;
    sym.resolved    = in[sym.section_idx].out_addr + sym.value;

    /* .data starts after .text, so resolved > 0x400100 */
    ASSERT(sym.resolved > 0x400100);
    /* verify the offset within .data is correct */
    ASSERT_EQ(sym.resolved - in[1].out_addr, 0x10);
}

/* ===== file offset assignment tests ===== */

TEST(file_offset_starts_at_base)
{
    struct input_section in[1];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x100;

    merge_sections(in, 1, out, 4, 0x400000, 0x1000);
    ASSERT_EQ(out[0].offset, 0x1000);
    ASSERT_EQ(in[0].out_offset, 0x1000);
}

TEST(file_offset_aligned)
{
    /*
     * .data with align=8 must have an 8-byte-aligned file offset.
     */
    struct input_section in[2];
    struct merged_sec out[4];

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    in[0].name  = ".text";
    in[0].type  = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4;
    in[0].size  = 0x103; /* not 8-aligned */

    in[1].name  = ".data";
    in[1].type  = SHT_PROGBITS;
    in[1].flags = SHF_ALLOC | SHF_WRITE;
    in[1].align = 8;
    in[1].size  = 0x40;

    merge_sections(in, 2, out, 4, 0x400000, 0x1000);
    ASSERT_EQ(out[1].offset % 8, 0);
}

/* ===== full layout integration ===== */

TEST(full_layout_text_data_bss)
{
    /*
     * Complete layout with .text, .data, .bss from two objects.
     *
     * A real linker groups input sections by name before laying them out.
     * We feed them pre-sorted: all .text first, then .data, then .bss.
     */
    struct input_section in[4];
    struct merged_sec out[8];
    int n;

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    /* obj0 .text */
    in[0].name  = ".text"; in[0].type = SHT_PROGBITS;
    in[0].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[0].align = 4; in[0].size = 0x200; in[0].obj_idx = 0;

    /* obj1 .text */
    in[1].name  = ".text"; in[1].type = SHT_PROGBITS;
    in[1].flags = SHF_ALLOC | SHF_EXECINSTR;
    in[1].align = 4; in[1].size = 0x100; in[1].obj_idx = 1;

    /* obj0 .data */
    in[2].name  = ".data"; in[2].type = SHT_PROGBITS;
    in[2].flags = SHF_ALLOC | SHF_WRITE;
    in[2].align = 8; in[2].size = 0x30; in[2].obj_idx = 0;

    /* obj1 .bss */
    in[3].name  = ".bss"; in[3].type = SHT_NOBITS;
    in[3].flags = SHF_ALLOC | SHF_WRITE;
    in[3].align = 8; in[3].size = 0x400; in[3].obj_idx = 1;

    n = merge_sections(in, 4, out, 8, 0x400000, 0x1000);

    /* should have 3 merged sections: .text, .data, .bss */
    ASSERT_EQ(n, 3);
    ASSERT_STR_EQ(out[0].name, ".text");
    ASSERT_STR_EQ(out[1].name, ".data");
    ASSERT_STR_EQ(out[2].name, ".bss");

    /* .text merged: 0x200 + 0x100 = 0x300 */
    ASSERT_EQ(out[0].size, 0x300);

    /* all sections are properly ordered in virtual address space */
    ASSERT(out[1].vaddr >= out[0].vaddr + out[0].size);
    ASSERT(out[2].vaddr >= out[1].vaddr + out[1].size);

    /* .bss type is NOBITS */
    ASSERT_EQ(out[2].type, SHT_NOBITS);
}

/* ===== main ===== */

int main(void)
{
    printf("test_layout:\n");

    /* section merging */
    RUN_TEST(merge_single_text);
    RUN_TEST(merge_two_text_sections);
    RUN_TEST(merge_text_and_data);
    RUN_TEST(merge_three_objects);
    RUN_TEST(merge_skips_non_alloc);

    /* virtual address assignment */
    RUN_TEST(vaddr_starts_at_base);
    RUN_TEST(vaddr_sequential);
    RUN_TEST(vaddr_with_bss);

    /* alignment */
    RUN_TEST(align_up_basic);
    RUN_TEST(align_up_page);
    RUN_TEST(align_up_zero_align);
    RUN_TEST(section_alignment_respected);
    RUN_TEST(merge_alignment_promotion);

    /* program headers */
    RUN_TEST(phdr_text_segment);
    RUN_TEST(phdr_data_segment);
    RUN_TEST(two_pt_load_segments);
    RUN_TEST(phdr_null_type);
    RUN_TEST(ehdr_phdr_fields_for_exec);

    /* entry point resolution */
    RUN_TEST(entry_point_from_start_symbol);
    RUN_TEST(entry_point_with_offset);
    RUN_TEST(entry_point_in_second_object);
    RUN_TEST(multiple_symbols_resolve);
    RUN_TEST(symbol_in_data_section);

    /* file offset */
    RUN_TEST(file_offset_starts_at_base);
    RUN_TEST(file_offset_aligned);

    /* integration */
    RUN_TEST(full_layout_text_data_bss);

    TEST_SUMMARY();
    return tests_failed;
}
