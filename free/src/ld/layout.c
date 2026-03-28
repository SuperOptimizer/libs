/*
 * layout.c - Section merging and segment layout for the free linker
 * Merges sections from multiple object files and assigns virtual addresses.
 * Includes --gc-sections support: builds a reference graph from relocations,
 * marks reachable sections from the entry point, discards unreachable ones.
 * Pure C89.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ld_internal.h"

#define BASE_ADDR   0x400000UL
#define PAGE_SIZE   0x1000UL

/* well-known section names for merging */
static const char *merge_order[] = {
    ".text", ".rodata", ".data", ".bss", NULL
};

static unsigned long align_up(unsigned long v, unsigned long align)
{
    if (align == 0) {
        return v;
    }
    return (v + align - 1) & ~(align - 1);
}

static void *xcalloc(unsigned long count, unsigned long size)
{
    void *p = calloc(count, size);
    if (!p) {
        fprintf(stderr, "ld: out of memory\n");
        exit(1);
    }
    return p;
}

/*
 * Check whether a section should be included in the output.
 * We only take SHF_ALLOC sections that are PROGBITS or NOBITS.
 */
static int is_alloc_section(Elf64_Shdr *sh)
{
    if (!(sh->sh_flags & SHF_ALLOC)) {
        return 0;
    }
    if (sh->sh_type != SHT_PROGBITS && sh->sh_type != SHT_NOBITS) {
        return 0;
    }
    return 1;
}

/*
 * Find the index in merge_order for a section name.
 * Returns -1 if not in the list (will be appended after known sections).
 */
static int merge_index(const char *name)
{
    int i;

    for (i = 0; merge_order[i]; i++) {
        if (strcmp(name, merge_order[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/* ---- gc-sections implementation ---- */

/*
 * gc_sections - Mark reachable sections starting from the entry point.
 *
 * Algorithm:
 *   1. Find the section containing the entry symbol.
 *   2. Use a worklist to propagate reachability through relocations.
 *   3. For each reachable section, follow its relocations to find
 *      referenced sections and add them to the worklist.
 *   4. Sections with gc_live==0 after marking are dead and will be
 *      skipped during layout.
 *
 * We use a flat worklist encoded as (obj_idx, sec_idx) pairs.
 */

struct gc_work {
    int obj_idx;
    int sec_idx;
};

void gc_sections(struct elf_obj *objs, int num_objs,
                 const char *entry_name)
{
    struct gc_work *worklist;
    int wl_count;
    int wl_cap;
    int wl_head;
    int i;
    int j;
    int total_discarded;

    /* initialize all sections as dead */
    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_sections; j++) {
            objs[i].sections[j].gc_live = 0;
        }
    }

    wl_cap = 256;
    worklist = (struct gc_work *)xcalloc(
        (unsigned long)wl_cap, sizeof(struct gc_work));
    wl_count = 0;
    wl_head = 0;

    /*
     * Seed the worklist: find the entry symbol's section.
     * Also seed any sections whose names start with ".init" or ".fini"
     * since those are typically needed regardless.
     */
    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_symbols; j++) {
            Elf64_Sym *sym = &objs[i].symbols[j].sym;
            int bind = ELF64_ST_BIND(sym->st_info);
            int sec_idx;

            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            if (objs[i].symbols[j].name[0] == '\0') {
                continue;
            }
            if (sym->st_shndx == SHN_UNDEF || sym->st_shndx == SHN_ABS) {
                continue;
            }
            if (strcmp(objs[i].symbols[j].name, entry_name) != 0) {
                continue;
            }

            sec_idx = (int)sym->st_shndx;
            if (sec_idx < objs[i].num_sections &&
                !objs[i].sections[sec_idx].gc_live) {
                objs[i].sections[sec_idx].gc_live = 1;
                if (wl_count >= wl_cap) {
                    wl_cap *= 2;
                    worklist = (struct gc_work *)realloc(worklist,
                        (unsigned long)wl_cap * sizeof(struct gc_work));
                }
                worklist[wl_count].obj_idx = i;
                worklist[wl_count].sec_idx = sec_idx;
                wl_count++;
            }
        }
    }

    /* also seed .init*, .fini*, and non-per-function sections */
    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_sections; j++) {
            struct section *sec = &objs[i].sections[j];

            if (!is_alloc_section(&sec->shdr)) {
                continue;
            }
            if (sec->gc_live) {
                continue;
            }
            if (sec->name == NULL) {
                continue;
            }
            /* keep .init, .fini, .ctors, .dtors, .init_array, .fini_array */
            if (strncmp(sec->name, ".init", 5) == 0 ||
                strncmp(sec->name, ".fini", 5) == 0 ||
                strncmp(sec->name, ".ctors", 6) == 0 ||
                strncmp(sec->name, ".dtors", 6) == 0) {
                sec->gc_live = 1;
                if (wl_count >= wl_cap) {
                    wl_cap *= 2;
                    worklist = (struct gc_work *)realloc(worklist,
                        (unsigned long)wl_cap * sizeof(struct gc_work));
                }
                worklist[wl_count].obj_idx = i;
                worklist[wl_count].sec_idx = j;
                wl_count++;
            }
        }
    }

    /* propagate reachability through relocations */
    while (wl_head < wl_count) {
        int oi = worklist[wl_head].obj_idx;
        int si = worklist[wl_head].sec_idx;
        struct elf_obj *obj = &objs[oi];

        wl_head++;

        /* scan all relocations in this object that target section si */
        for (j = 0; j < obj->num_relas; j++) {
            struct elf_rela *r = &obj->relas[j];
            int sym_idx;
            Elf64_Sym *sym;
            int ref_sec;

            if (r->target_section != si) {
                continue;
            }

            sym_idx = (int)ELF64_R_SYM(r->rela.r_info);
            if (sym_idx < 0 || sym_idx >= obj->num_symbols) {
                continue;
            }

            sym = &obj->symbols[sym_idx].sym;

            /* for local/section symbols, the referenced section is
             * directly from st_shndx */
            if (sym->st_shndx == SHN_UNDEF || sym->st_shndx == SHN_ABS) {
                /* for global undefined syms, find the definition */
                int bind = ELF64_ST_BIND(sym->st_info);
                if ((bind == STB_GLOBAL || bind == STB_WEAK) &&
                    sym->st_shndx == SHN_UNDEF &&
                    obj->symbols[sym_idx].name[0] != '\0') {
                    /* search all objects for the definition */
                    int oi2;
                    int sj2;
                    for (oi2 = 0; oi2 < num_objs; oi2++) {
                        for (sj2 = 0; sj2 < objs[oi2].num_symbols; sj2++) {
                            Elf64_Sym *ds = &objs[oi2].symbols[sj2].sym;
                            if (ds->st_shndx == SHN_UNDEF ||
                                ds->st_shndx == SHN_ABS) {
                                continue;
                            }
                            if (strcmp(objs[oi2].symbols[sj2].name,
                                      obj->symbols[sym_idx].name) != 0) {
                                continue;
                            }
                            ref_sec = (int)ds->st_shndx;
                            if (ref_sec < objs[oi2].num_sections &&
                                !objs[oi2].sections[ref_sec].gc_live) {
                                objs[oi2].sections[ref_sec].gc_live = 1;
                                if (wl_count >= wl_cap) {
                                    wl_cap *= 2;
                                    worklist = (struct gc_work *)realloc(
                                        worklist,
                                        (unsigned long)wl_cap *
                                        sizeof(struct gc_work));
                                }
                                worklist[wl_count].obj_idx = oi2;
                                worklist[wl_count].sec_idx = ref_sec;
                                wl_count++;
                            }
                            break;
                        }
                    }
                }
                continue;
            }

            ref_sec = (int)sym->st_shndx;
            if (ref_sec < obj->num_sections &&
                !obj->sections[ref_sec].gc_live) {
                obj->sections[ref_sec].gc_live = 1;
                if (wl_count >= wl_cap) {
                    wl_cap *= 2;
                    worklist = (struct gc_work *)realloc(worklist,
                        (unsigned long)wl_cap * sizeof(struct gc_work));
                }
                worklist[wl_count].obj_idx = oi;
                worklist[wl_count].sec_idx = ref_sec;
                wl_count++;
            }
        }
    }

    free(worklist);

    /* count discarded sections for diagnostic */
    total_discarded = 0;
    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_sections; j++) {
            struct section *sec = &objs[i].sections[j];
            if (!is_alloc_section(&sec->shdr)) {
                continue;
            }
            if (!sec->gc_live) {
                total_discarded++;
            }
        }
    }

    if (total_discarded > 0) {
        fprintf(stderr, "ld: --gc-sections: discarded %d unreachable "
                        "sections\n", total_discarded);
    }
}

/* ---- public interface ---- */

void layout_sections(struct elf_obj *objs, int num_objs,
                     struct merged_section **out_msecs, int *out_num_msecs,
                     Elf64_Phdr *out_phdrs, int *out_num_phdrs)
{
    struct merged_section *msecs;
    int nmsecs;
    int msec_cap;
    int i;
    int j;
    int k;
    u64 file_offset;
    u64 vaddr;
    u64 text_seg_filesz;
    u64 data_seg_start;
    u64 data_seg_file_start;
    u64 data_seg_filesz;
    u64 data_seg_memsz;
    u64 hdr_size;

    msec_cap = 16;
    msecs = (struct merged_section *)xcalloc(
        (unsigned long)msec_cap, sizeof(struct merged_section));
    nmsecs = 0;

    /*
     * Phase 1: Collect all allocatable sections and group by name.
     * Process in merge_order first, then any remaining.
     * Skip sections marked dead by gc-sections (gc_live == 0).
     */

    /* process known sections in order */
    for (k = 0; merge_order[k]; k++) {
        int found = 0;

        for (i = 0; i < num_objs; i++) {
            for (j = 0; j < objs[i].num_sections; j++) {
                struct section *sec = &objs[i].sections[j];
                u64 sec_align;
                u64 cur_size;
                u64 aligned_off;
                int mi;

                if (!is_alloc_section(&sec->shdr)) {
                    continue;
                }
                if (strcmp(sec->name, merge_order[k]) != 0) {
                    continue;
                }

                /* skip gc-dead sections */
                if (!sec->gc_live) {
                    continue;
                }

                /* find or create merged section */
                mi = -1;
                {
                    int m;
                    for (m = 0; m < nmsecs; m++) {
                        if (strcmp(msecs[m].name, sec->name) == 0) {
                            mi = m;
                            break;
                        }
                    }
                }
                if (mi < 0) {
                    if (nmsecs >= msec_cap) {
                        msec_cap *= 2;
                        msecs = (struct merged_section *)realloc(msecs,
                            (unsigned long)msec_cap *
                            sizeof(struct merged_section));
                        if (!msecs) {
                            fprintf(stderr, "ld: out of memory\n");
                            exit(1);
                        }
                    }
                    mi = nmsecs++;
                    memset(&msecs[mi], 0, sizeof(struct merged_section));
                    msecs[mi].name = merge_order[k];
                    msecs[mi].shdr.sh_type = sec->shdr.sh_type;
                    msecs[mi].shdr.sh_flags = sec->shdr.sh_flags;
                    msecs[mi].shdr.sh_addralign = sec->shdr.sh_addralign;
                    msecs[mi].data = NULL;
                    msecs[mi].size = 0;
                    msecs[mi].capacity = 0;
                    msecs[mi].num_inputs = 0;
                    msecs[mi].input_cap = 0;
                    msecs[mi].inputs = NULL;
                }

                found = 1;

                /* merge section flags from all inputs */
                msecs[mi].shdr.sh_flags |=
                    (sec->shdr.sh_flags &
                     (SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR));

                /* align within the merged section */
                sec_align = sec->shdr.sh_addralign;
                if (sec_align < 1) {
                    sec_align = 1;
                }
                if (sec_align > msecs[mi].shdr.sh_addralign) {
                    msecs[mi].shdr.sh_addralign = sec_align;
                }

                cur_size = msecs[mi].size;
                aligned_off = align_up(cur_size, (unsigned long)sec_align);

                /* record input piece */
                if (msecs[mi].num_inputs >= msecs[mi].input_cap) {
                    int new_cap;
                    new_cap = msecs[mi].input_cap == 0
                        ? 16 : msecs[mi].input_cap * 2;
                    msecs[mi].inputs = (struct input_piece *)realloc(
                        msecs[mi].inputs,
                        (unsigned long)new_cap * sizeof(struct input_piece));
                    if (!msecs[mi].inputs) {
                        fprintf(stderr, "ld: out of memory\n");
                        exit(1);
                    }
                    msecs[mi].input_cap = new_cap;
                }
                {
                    struct input_piece *ip =
                        &msecs[mi].inputs[msecs[mi].num_inputs++];
                    ip->obj_idx = i;
                    ip->sec_idx = j;
                    ip->offset_in_merged = aligned_off;
                }

                /* copy data if PROGBITS */
                if (sec->shdr.sh_type == SHT_PROGBITS && sec->data) {
                    unsigned long new_size;
                    new_size = (unsigned long)(aligned_off + sec->shdr.sh_size);
                    if (new_size > msecs[mi].capacity) {
                        unsigned long nc;
                        nc = msecs[mi].capacity == 0
                            ? 4096 : msecs[mi].capacity;
                        while (nc < new_size) {
                            nc *= 2;
                        }
                        msecs[mi].data = (u8 *)realloc(
                            msecs[mi].data, nc);
                        if (!msecs[mi].data) {
                            fprintf(stderr, "ld: out of memory\n");
                            exit(1);
                        }
                        /* zero fill gap */
                        if (nc > msecs[mi].capacity) {
                            memset(msecs[mi].data + msecs[mi].capacity, 0,
                                   (unsigned long)(nc - msecs[mi].capacity));
                        }
                        msecs[mi].capacity = nc;
                    }
                    /* zero pad alignment gap */
                    if (aligned_off > cur_size) {
                        memset(msecs[mi].data + cur_size, 0,
                               (unsigned long)(aligned_off - cur_size));
                    }
                    memcpy(msecs[mi].data + aligned_off, sec->data,
                           (unsigned long)sec->shdr.sh_size);
                    msecs[mi].size = new_size;
                } else {
                    /* NOBITS */
                    unsigned long new_size;
                    new_size = (unsigned long)(aligned_off + sec->shdr.sh_size);
                    if (new_size > msecs[mi].size) {
                        msecs[mi].size = new_size;
                    }
                }
            }
        }
        (void)found;
    }

    /* process any remaining alloc sections not in merge_order */
    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_sections; j++) {
            struct section *sec = &objs[i].sections[j];
            int mi;
            u64 sec_align;
            u64 cur_size;
            u64 aligned_off;

            if (!is_alloc_section(&sec->shdr)) {
                continue;
            }
            if (merge_index(sec->name) >= 0) {
                continue;
            }
            /* skip gc-dead sections */
            if (!sec->gc_live) {
                continue;
            }

            /* find or create merged section */
            mi = -1;
            {
                int m;
                for (m = 0; m < nmsecs; m++) {
                    if (strcmp(msecs[m].name, sec->name) == 0) {
                        mi = m;
                        break;
                    }
                }
            }
            if (mi < 0) {
                if (nmsecs >= msec_cap) {
                    msec_cap *= 2;
                    msecs = (struct merged_section *)realloc(msecs,
                        (unsigned long)msec_cap *
                        sizeof(struct merged_section));
                    if (!msecs) {
                        fprintf(stderr, "ld: out of memory\n");
                        exit(1);
                    }
                }
                mi = nmsecs++;
                memset(&msecs[mi], 0, sizeof(struct merged_section));
                msecs[mi].name = sec->name;
                msecs[mi].shdr.sh_type = sec->shdr.sh_type;
                msecs[mi].shdr.sh_flags = sec->shdr.sh_flags;
                msecs[mi].shdr.sh_addralign = sec->shdr.sh_addralign;
                msecs[mi].data = NULL;
                msecs[mi].size = 0;
                msecs[mi].capacity = 0;
                msecs[mi].num_inputs = 0;
                msecs[mi].input_cap = 0;
                msecs[mi].inputs = NULL;
            }

            /* merge section flags from all inputs */
            msecs[mi].shdr.sh_flags |=
                (sec->shdr.sh_flags &
                 (SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR));

            sec_align = sec->shdr.sh_addralign;
            if (sec_align < 1) {
                sec_align = 1;
            }
            if (sec_align > msecs[mi].shdr.sh_addralign) {
                msecs[mi].shdr.sh_addralign = sec_align;
            }

            cur_size = msecs[mi].size;
            aligned_off = align_up(cur_size, (unsigned long)sec_align);

            /* record input piece */
            if (msecs[mi].num_inputs >= msecs[mi].input_cap) {
                int new_cap;
                new_cap = msecs[mi].input_cap == 0
                    ? 16 : msecs[mi].input_cap * 2;
                msecs[mi].inputs = (struct input_piece *)realloc(
                    msecs[mi].inputs,
                    (unsigned long)new_cap * sizeof(struct input_piece));
                if (!msecs[mi].inputs) {
                    fprintf(stderr, "ld: out of memory\n");
                    exit(1);
                }
                msecs[mi].input_cap = new_cap;
            }
            {
                struct input_piece *ip =
                    &msecs[mi].inputs[msecs[mi].num_inputs++];
                ip->obj_idx = i;
                ip->sec_idx = j;
                ip->offset_in_merged = aligned_off;
            }

            if (sec->shdr.sh_type == SHT_PROGBITS && sec->data) {
                unsigned long new_size;
                new_size = (unsigned long)(aligned_off + sec->shdr.sh_size);
                if (new_size > msecs[mi].capacity) {
                    unsigned long nc;
                    nc = msecs[mi].capacity == 0 ? 4096 : msecs[mi].capacity;
                    while (nc < new_size) {
                        nc *= 2;
                    }
                    msecs[mi].data = (u8 *)realloc(msecs[mi].data, nc);
                    if (!msecs[mi].data) {
                        fprintf(stderr, "ld: out of memory\n");
                        exit(1);
                    }
                    if (nc > msecs[mi].capacity) {
                        memset(msecs[mi].data + msecs[mi].capacity, 0,
                               (unsigned long)(nc - msecs[mi].capacity));
                    }
                    msecs[mi].capacity = nc;
                }
                if (aligned_off > cur_size) {
                    memset(msecs[mi].data + cur_size, 0,
                           (unsigned long)(aligned_off - cur_size));
                }
                memcpy(msecs[mi].data + aligned_off, sec->data,
                       (unsigned long)sec->shdr.sh_size);
                msecs[mi].size = new_size;
            } else {
                unsigned long new_size;
                new_size = (unsigned long)(aligned_off + sec->shdr.sh_size);
                if (new_size > msecs[mi].size) {
                    msecs[mi].size = new_size;
                }
            }
        }
    }

    /*
     * Phase 2: Assign file offsets and virtual addresses.
     *
     * Layout:
     *   [ELF header][program headers][.text][.rodata] ... [.data][.bss]
     *
     * Segment 0 (PT_LOAD RX): .text, .rodata (and other RX/R sections)
     * Segment 1 (PT_LOAD RW): .data, .bss (and other RW sections)
     */

    hdr_size = align_up(sizeof(Elf64_Ehdr) + 2 * sizeof(Elf64_Phdr),
                        PAGE_SIZE);
    file_offset = hdr_size;

    /* vaddr must be congruent with file_offset mod PAGE_SIZE */
    vaddr = BASE_ADDR + file_offset;

    text_seg_filesz = 0;
    data_seg_start = 0;
    data_seg_file_start = 0;
    data_seg_filesz = 0;
    data_seg_memsz = 0;

    /* assign addresses to text/readonly sections first */
    for (i = 0; i < nmsecs; i++) {
        u64 align;

        /* skip writable sections for now */
        if (msecs[i].shdr.sh_flags & SHF_WRITE) {
            continue;
        }

        align = msecs[i].shdr.sh_addralign;
        if (align < 1) {
            align = 1;
        }

        file_offset = align_up((unsigned long)file_offset,
                               (unsigned long)align);
        vaddr = align_up((unsigned long)vaddr, (unsigned long)align);

        msecs[i].shdr.sh_offset = file_offset;
        msecs[i].shdr.sh_addr = vaddr;
        msecs[i].shdr.sh_size = msecs[i].size;

        /* update input section out_addr for symbol resolution */
        for (j = 0; j < msecs[i].num_inputs; j++) {
            struct input_piece *ip = &msecs[i].inputs[j];
            objs[ip->obj_idx].sections[ip->sec_idx].out_addr =
                vaddr + ip->offset_in_merged;
        }

        if (msecs[i].shdr.sh_type != SHT_NOBITS) {
            file_offset += msecs[i].size;
        }
        vaddr += msecs[i].size;
    }

    text_seg_filesz = file_offset - hdr_size;

    /* new page for data segment */
    file_offset = align_up((unsigned long)file_offset, PAGE_SIZE);
    vaddr = align_up((unsigned long)vaddr, PAGE_SIZE);
    data_seg_start = vaddr;
    data_seg_file_start = file_offset;

    /* assign addresses to writable sections */
    for (i = 0; i < nmsecs; i++) {
        u64 align;

        if (!(msecs[i].shdr.sh_flags & SHF_WRITE)) {
            continue;
        }

        align = msecs[i].shdr.sh_addralign;
        if (align < 1) {
            align = 1;
        }

        file_offset = align_up((unsigned long)file_offset,
                               (unsigned long)align);
        vaddr = align_up((unsigned long)vaddr, (unsigned long)align);

        msecs[i].shdr.sh_offset = file_offset;
        msecs[i].shdr.sh_addr = vaddr;
        msecs[i].shdr.sh_size = msecs[i].size;

        for (j = 0; j < msecs[i].num_inputs; j++) {
            struct input_piece *ip = &msecs[i].inputs[j];
            objs[ip->obj_idx].sections[ip->sec_idx].out_addr =
                vaddr + ip->offset_in_merged;
        }

        if (msecs[i].shdr.sh_type != SHT_NOBITS) {
            file_offset += msecs[i].size;
            data_seg_filesz = file_offset - data_seg_file_start;
        }
        vaddr += msecs[i].size;
        data_seg_memsz = vaddr - data_seg_start;
    }

    /*
     * Phase 3: Build program headers.
     */
    memset(out_phdrs, 0, 2 * sizeof(Elf64_Phdr));

    /* text segment (RX) */
    out_phdrs[0].p_type = PT_LOAD;
    out_phdrs[0].p_flags = PF_R | PF_X;
    out_phdrs[0].p_offset = 0;
    out_phdrs[0].p_vaddr = BASE_ADDR;
    out_phdrs[0].p_paddr = BASE_ADDR;
    out_phdrs[0].p_filesz = hdr_size + text_seg_filesz;
    out_phdrs[0].p_memsz = out_phdrs[0].p_filesz;
    out_phdrs[0].p_align = PAGE_SIZE;

    *out_num_phdrs = 1;

    /* data segment (RW) - only if there are writable sections */
    if (data_seg_memsz > 0) {
        out_phdrs[1].p_type = PT_LOAD;
        out_phdrs[1].p_flags = PF_R | PF_W;
        out_phdrs[1].p_offset = data_seg_file_start;
        out_phdrs[1].p_vaddr = data_seg_start;
        out_phdrs[1].p_paddr = data_seg_start;
        out_phdrs[1].p_filesz = data_seg_filesz;
        out_phdrs[1].p_memsz = data_seg_memsz;
        out_phdrs[1].p_align = PAGE_SIZE;
        *out_num_phdrs = 2;
    }

    *out_msecs = msecs;
    *out_num_msecs = nmsecs;
}

void layout_free(struct merged_section *msecs, int num_msecs)
{
    int i;

    for (i = 0; i < num_msecs; i++) {
        if (msecs[i].data) {
            free(msecs[i].data);
        }
        if (msecs[i].inputs) {
            free(msecs[i].inputs);
        }
    }
    free(msecs);
}
