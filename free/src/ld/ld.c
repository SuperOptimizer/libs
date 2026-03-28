/*
 * ld.c - Linker driver for the free toolchain
 * Usage: free-ld [-o output] [-e entry] input1.o input2.o ...
 *
 * Kernel-oriented features:
 *   -r / --relocatable   Partial linking (output ET_REL)
 *   --gc-sections        Garbage collect unused sections
 *   --whole-archive / --no-whole-archive  Force all archive members
 *   --emit-relocs        Keep relocations in final output
 *   -Map file            Generate linker map file
 *   --build-id           Generate .note.gnu.build-id section
 *
 * Supports LTO: detects .free_ir sections in .o files and runs
 * whole-program optimization before final linking.
 * Pure C89.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ld_internal.h"
#include "ir.h"

/* ---- usage ---- */

static void usage(void)
{
    fprintf(stderr,
        "usage: free-ld [-o output] [-e entry] [-shared] [-pie] "
        "[-static]\n"
        "       [-T script.ld] [-L dir] [-l lib]\n"
        "       [-r | --relocatable] [--gc-sections] "
        "[--whole-archive] [--no-whole-archive]\n"
        "       [--emit-relocs] [-Map file] [--build-id] "
        "[--defsym sym=val]\n"
        "       [--sort-section=name] [-z flag] "
        "[--no-undefined]\n"
        "       [--version-script file] "
        "input.o ...\n");
    exit(1);
}

/* ---- helpers ---- */

static void *xmalloc(unsigned long n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "ld: out of memory\n");
        exit(1);
    }
    return p;
}

static void *xcalloc_ld(unsigned long count, unsigned long size)
{
    void *p = calloc(count, size);
    if (!p) {
        fprintf(stderr, "ld: out of memory\n");
        exit(1);
    }
    return p;
}

/* ---- AR archive reading for --whole-archive ---- */

/*
 * is_ar_file - check if the given path is an AR archive.
 * Returns 1 if the file starts with "!<arch>\n".
 */
static int is_ar_file(const char *path)
{
    FILE *f;
    char buf[8];
    unsigned long n;

    f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    n = fread(buf, 1, 8, f);
    fclose(f);
    if (n < 8) {
        return 0;
    }
    return (memcmp(buf, AR_MAGIC, AR_MAGIC_LEN) == 0);
}

/*
 * Parse a decimal field from the AR header, stopping at spaces or end.
 */
static unsigned long ar_parse_size(const char *field, int len)
{
    unsigned long val = 0;
    int i;

    for (i = 0; i < len; i++) {
        if (field[i] < '0' || field[i] > '9') {
            break;
        }
        val = val * 10 + (unsigned long)(field[i] - '0');
    }
    return val;
}

/*
 * Extract all .o members from an AR archive.
 * Writes extracted .o files to temporary files and appends their paths
 * to the output array.
 */
static void extract_archive_all(const char *archive_path,
                                const char ***out_paths,
                                int *out_count, int *out_cap)
{
    FILE *f;
    char magic[8];
    unsigned long n;

    f = fopen(archive_path, "rb");
    if (!f) {
        fprintf(stderr, "ld: cannot open archive '%s'\n", archive_path);
        exit(1);
    }
    n = fread(magic, 1, 8, f);
    if (n < 8 || memcmp(magic, AR_MAGIC, AR_MAGIC_LEN) != 0) {
        fprintf(stderr, "ld: '%s' is not an AR archive\n", archive_path);
        fclose(f);
        exit(1);
    }

    while (!feof(f)) {
        Ar_hdr hdr;
        unsigned long member_size;
        char name_buf[256];
        int name_len;
        u8 *member_data;
        char *tmp_path;
        FILE *tmp_f;
        int i;

        n = fread(&hdr, 1, sizeof(Ar_hdr), f);
        if (n < sizeof(Ar_hdr)) {
            break;
        }

        /* check fmag */
        if (hdr.ar_fmag[0] != '`' || hdr.ar_fmag[1] != '\n') {
            break;
        }

        member_size = ar_parse_size(hdr.ar_size, 10);
        if (member_size == 0) {
            break;
        }

        /* extract member name */
        name_len = 0;
        for (i = 0; i < 16 && hdr.ar_name[i] != '/' &&
             hdr.ar_name[i] != ' '; i++) {
            name_buf[name_len++] = hdr.ar_name[i];
        }
        name_buf[name_len] = '\0';

        /* skip symbol table and string table entries */
        if (name_len == 0 || strcmp(name_buf, "/") == 0 ||
            strcmp(name_buf, "//") == 0 ||
            hdr.ar_name[0] == '/') {
            fseek(f, (long)member_size, SEEK_CUR);
            if (member_size & 1) {
                fseek(f, 1, SEEK_CUR);
            }
            continue;
        }

        /* read member data */
        member_data = (u8 *)xmalloc(member_size);
        n = fread(member_data, 1, member_size, f);
        if (n < member_size) {
            free(member_data);
            break;
        }
        /* skip padding byte */
        if (member_size & 1) {
            fseek(f, 1, SEEK_CUR);
        }

        /* check if it is an ELF object */
        if (member_size < sizeof(Elf64_Ehdr) ||
            member_data[0] != ELFMAG0 ||
            member_data[1] != ELFMAG1 ||
            member_data[2] != ELFMAG2 ||
            member_data[3] != ELFMAG3) {
            free(member_data);
            continue;
        }

        /* write to a temp file */
        {
            unsigned long path_len;
            path_len = strlen(archive_path) + name_len + 32;
            tmp_path = (char *)xmalloc(path_len);
            sprintf(tmp_path, "/tmp/ld_ar_%s_%s", name_buf,
                    "XXXXXX");
        }
        /* simple unique naming: use member name + counter */
        {
            static int ar_counter = 0;
            unsigned long path_len;
            path_len = strlen(archive_path) + name_len + 64;
            free(tmp_path);
            tmp_path = (char *)xmalloc(path_len);
            sprintf(tmp_path, "/tmp/ld_ar_%d_%s", ar_counter++, name_buf);
        }

        tmp_f = fopen(tmp_path, "wb");
        if (!tmp_f) {
            fprintf(stderr, "ld: cannot create temp file '%s'\n", tmp_path);
            free(member_data);
            free(tmp_path);
            continue;
        }
        fwrite(member_data, 1, member_size, tmp_f);
        fclose(tmp_f);
        free(member_data);

        /* add to output list */
        if (*out_count >= *out_cap) {
            *out_cap *= 2;
            *out_paths = (const char **)realloc((void *)*out_paths,
                (unsigned long)*out_cap * sizeof(const char *));
        }
        (*out_paths)[(*out_count)++] = tmp_path;
    }

    fclose(f);
}

/* ---- LTO support ---- */

#define LTO_ARENA_SIZE (8 * 1024 * 1024)

/*
 * has_free_ir_section - check if an ELF .o file contains a .free_ir
 * section by scanning section names.
 */
static int has_free_ir_section(struct elf_obj *obj)
{
    int i;

    for (i = 0; i < obj->num_sections; i++) {
        if (obj->sections[i].name != NULL &&
            strcmp(obj->sections[i].name, ".free_ir") == 0) {
            return 1;
        }
    }
    return 0;
}

/* count functions in an IR module */
static int ir_count_funcs(struct ir_module *mod)
{
    int n = 0;
    struct ir_func *f;
    for (f = mod->funcs; f; f = f->next) n++;
    return n;
}

/* count globals in an IR module */
static int ir_count_globals(struct ir_module *mod)
{
    int n = 0;
    struct ir_global *g;
    for (g = mod->globals; g; g = g->next) n++;
    return n;
}

/*
 * try_lto - Attempt LTO on input objects.
 * Scans all inputs for .free_ir sections. If any are found, reads
 * the IR, merges the modules, runs LTO optimizations, and logs
 * the result.
 * Returns 1 if LTO was performed, 0 otherwise.
 */
static int try_lto(const char **inputs, int num_inputs,
                   struct elf_obj *objs)
{
    struct ir_module **modules;
    struct ir_module *merged;
    int nmodules;
    int i;
    int has_lto;
    char *arena_buf;
    struct arena lto_arena;

    /* check if any input has .free_ir */
    has_lto = 0;
    for (i = 0; i < num_inputs; i++) {
        if (has_free_ir_section(&objs[i])) {
            has_lto = 1;
            break;
        }
    }
    if (!has_lto) {
        return 0;
    }

    fprintf(stderr, "ld: LTO: found .free_ir sections, "
                    "running whole-program optimization\n");

    /* set up arena for LTO */
    arena_buf = (char *)malloc(LTO_ARENA_SIZE);
    if (!arena_buf) {
        fprintf(stderr, "ld: LTO: out of memory\n");
        return 0;
    }
    arena_init(&lto_arena, arena_buf, LTO_ARENA_SIZE);

    /* read IR from all objects that have it */
    modules = (struct ir_module **)malloc(
        (unsigned long)num_inputs * sizeof(struct ir_module *));
    nmodules = 0;

    for (i = 0; i < num_inputs; i++) {
        struct ir_module *mod;
        mod = ir_read_from_elf(inputs[i], &lto_arena);
        if (mod != NULL) {
            modules[nmodules++] = mod;
            fprintf(stderr, "ld: LTO: read IR from '%s' "
                            "(%d funcs, %d globals)\n",
                    inputs[i], ir_count_funcs(mod), ir_count_globals(mod));
        }
    }

    if (nmodules == 0) {
        free(modules);
        free(arena_buf);
        return 0;
    }

    /* merge all IR modules */
    merged = lto_merge(modules, nmodules, &lto_arena);
    fprintf(stderr, "ld: LTO: merged %d modules -> %d funcs, %d globals\n",
            nmodules, ir_count_funcs(merged), ir_count_globals(merged));

    /* run LTO optimizations */
    lto_optimize(merged);
    fprintf(stderr, "ld: LTO: after optimization -> %d funcs, %d globals\n",
            ir_count_funcs(merged), ir_count_globals(merged));

    free(modules);
    free(arena_buf);
    return 1;
}

/* ---- build-id computation ---- */

/*
 * Simple FNV-1a hash for build-id generation.
 * Hashes all section data in the output to produce a 20-byte
 * (160-bit) build-id, matching the default SHA-1-length build-id
 * used by GNU ld.
 */
static void compute_build_id(struct out_section *secs, int nsecs,
                             u8 *out_hash)
{
    u64 h1;
    u64 h2;
    u64 h3;
    int i;
    unsigned long j;

    /* use three different FNV-1a seeds to get 24 bytes, take 20 */
    h1 = 0xcbf29ce484222325UL;
    h2 = 0xd15ea5e0deadbeefUL;
    h3 = 0x0123456789abcdefUL;

    for (i = 0; i < nsecs; i++) {
        if (secs[i].data == NULL || secs[i].size == 0) {
            continue;
        }
        for (j = 0; j < secs[i].size; j++) {
            h1 ^= secs[i].data[j];
            h1 *= 0x100000001b3UL;
            h2 ^= secs[i].data[j];
            h2 *= 0x100000001b3UL;
            h3 ^= secs[i].data[j];
            h3 *= 0x100000001b3UL;
        }
    }

    /* pack into 20 bytes */
    out_hash[0]  = (u8)(h1 & 0xff);
    out_hash[1]  = (u8)((h1 >> 8) & 0xff);
    out_hash[2]  = (u8)((h1 >> 16) & 0xff);
    out_hash[3]  = (u8)((h1 >> 24) & 0xff);
    out_hash[4]  = (u8)((h1 >> 32) & 0xff);
    out_hash[5]  = (u8)((h1 >> 40) & 0xff);
    out_hash[6]  = (u8)((h1 >> 48) & 0xff);
    out_hash[7]  = (u8)((h1 >> 56) & 0xff);
    out_hash[8]  = (u8)(h2 & 0xff);
    out_hash[9]  = (u8)((h2 >> 8) & 0xff);
    out_hash[10] = (u8)((h2 >> 16) & 0xff);
    out_hash[11] = (u8)((h2 >> 24) & 0xff);
    out_hash[12] = (u8)((h2 >> 32) & 0xff);
    out_hash[13] = (u8)((h2 >> 40) & 0xff);
    out_hash[14] = (u8)((h2 >> 48) & 0xff);
    out_hash[15] = (u8)((h2 >> 56) & 0xff);
    out_hash[16] = (u8)(h3 & 0xff);
    out_hash[17] = (u8)((h3 >> 8) & 0xff);
    out_hash[18] = (u8)((h3 >> 16) & 0xff);
    out_hash[19] = (u8)((h3 >> 24) & 0xff);
}

/*
 * build_note_build_id - construct a .note.gnu.build-id section.
 * The note format is:
 *   u32 namesz  (= 4 for "GNU\0")
 *   u32 descsz  (= 20 for SHA-1-length hash)
 *   u32 type    (= 3 = NT_GNU_BUILD_ID)
 *   char name[] (= "GNU\0", padded to 4-byte boundary)
 *   u8 desc[]   (the hash, padded to 4-byte boundary)
 */
#define NT_GNU_BUILD_ID 3

static u8 *build_note_build_id(struct out_section *secs, int nsecs,
                               u32 *out_size)
{
    u32 namesz;
    u32 descsz;
    u32 total;
    u8 *buf;
    u8 hash[20];

    namesz = 4;  /* "GNU\0" */
    descsz = 20; /* 160-bit hash */
    total = 12 + 4 + 20; /* header + name + desc */
    /* name already 4-byte aligned; desc 20 bytes -> pad to 4 */
    total = (total + 3) & ~(u32)3;

    buf = (u8 *)xcalloc_ld(total, 1);

    /* compute hash over output section data */
    compute_build_id(secs, nsecs, hash);

    /* namesz */
    buf[0] = (u8)(namesz & 0xff);
    buf[1] = (u8)((namesz >> 8) & 0xff);
    buf[2] = (u8)((namesz >> 16) & 0xff);
    buf[3] = (u8)((namesz >> 24) & 0xff);

    /* descsz */
    buf[4] = (u8)(descsz & 0xff);
    buf[5] = (u8)((descsz >> 8) & 0xff);
    buf[6] = (u8)((descsz >> 16) & 0xff);
    buf[7] = (u8)((descsz >> 24) & 0xff);

    /* type = NT_GNU_BUILD_ID */
    buf[8]  = (u8)(NT_GNU_BUILD_ID & 0xff);
    buf[9]  = 0;
    buf[10] = 0;
    buf[11] = 0;

    /* name = "GNU\0" */
    buf[12] = 'G';
    buf[13] = 'N';
    buf[14] = 'U';
    buf[15] = '\0';

    /* desc = hash */
    memcpy(buf + 16, hash, 20);

    *out_size = total;
    return buf;
}

/* ---- linker map file ---- */

/*
 * write_map_file - Generate a linker map showing section layout,
 * symbol addresses, and sizes.
 */
static void write_map_file(const char *map_path,
                           struct merged_section *msecs, int num_msecs,
                           struct elf_obj *objs, int num_objs,
                           u64 entry_addr)
{
    FILE *mf;
    int i;
    int j;

    mf = fopen(map_path, "w");
    if (!mf) {
        fprintf(stderr, "ld: cannot create map file '%s'\n", map_path);
        return;
    }

    fprintf(mf, "Linker Map\n");
    fprintf(mf, "==========\n\n");
    fprintf(mf, "Entry point: 0x%lx\n\n", (unsigned long)entry_addr);

    /* section layout */
    fprintf(mf, "Sections:\n");
    fprintf(mf, "  %-20s %-18s %-18s %-10s %s\n",
            "Name", "Address", "Offset", "Size", "Align");
    fprintf(mf, "  %-20s %-18s %-18s %-10s %s\n",
            "----", "-------", "------", "----", "-----");

    for (i = 0; i < num_msecs; i++) {
        fprintf(mf, "  %-20s 0x%016lx 0x%016lx 0x%08lx %lu\n",
                msecs[i].name,
                (unsigned long)msecs[i].shdr.sh_addr,
                (unsigned long)msecs[i].shdr.sh_offset,
                (unsigned long)msecs[i].size,
                (unsigned long)msecs[i].shdr.sh_addralign);

        /* show input contributions */
        for (j = 0; j < msecs[i].num_inputs; j++) {
            struct input_piece *ip = &msecs[i].inputs[j];
            struct section *sec =
                &objs[ip->obj_idx].sections[ip->sec_idx];
            fprintf(mf, "    %-16s %s(%s) size=0x%lx\n",
                    "",
                    objs[ip->obj_idx].filename,
                    sec->name,
                    (unsigned long)sec->shdr.sh_size);
        }
    }

    /* global symbols */
    fprintf(mf, "\nSymbols:\n");
    fprintf(mf, "  %-40s %-18s %-10s %s\n",
            "Name", "Address", "Size", "Source");
    fprintf(mf, "  %-40s %-18s %-10s %s\n",
            "----", "-------", "----", "------");

    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_symbols; j++) {
            Elf64_Sym *sym = &objs[i].symbols[j].sym;
            int bind = ELF64_ST_BIND(sym->st_info);

            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            if (objs[i].symbols[j].name[0] == '\0') {
                continue;
            }
            if (sym->st_shndx == SHN_UNDEF) {
                continue;
            }

            fprintf(mf, "  %-40s 0x%016lx 0x%08lx %s\n",
                    objs[i].symbols[j].name,
                    (unsigned long)objs[i].symbols[j].resolved_addr,
                    (unsigned long)sym->st_size,
                    objs[i].filename);
        }
    }

    fclose(mf);
}

/* ---- partial linking (-r / --relocatable) ---- */

/*
 * link_relocatable - merge multiple .o files into a single .o file,
 * preserving relocations. Produces an ET_REL output with:
 *   - merged sections
 *   - merged symbol table (with updated section indices)
 *   - rewritten relocations (with updated symbol indices and offsets)
 */
static int link_relocatable(const char *output, struct elf_obj *objs,
                            int num_objs)
{
    struct merged_section *msecs;
    int num_msecs;
    int msec_cap;
    int i;
    int j;
    int k;
    FILE *f;
    Elf64_Ehdr ehdr;

    /* output string tables */
    u8 *strtab;
    u32 strtab_size;
    u32 strtab_cap;

    u8 *shstrtab;
    u32 shstrtab_size;
    u32 shstrtab_cap;

    /* output symbol table */
    Elf64_Sym *out_syms;
    int out_nsyms;
    int out_sym_cap;

    /* mapping: for each (obj_idx, sym_idx), the output symbol index */
    int **sym_map;

    /* output relocations */
    Elf64_Rela *out_relas;
    int out_nrelas;
    int out_rela_cap;
    int *rela_target_sec; /* output section index for each rela group */

    /* output sections */
    int num_out_secs;
    Elf64_Shdr *out_shdrs;
    u64 file_offset;
    int symtab_idx;
    int strtab_idx;
    int shstrtab_idx;

    /* ---- Phase 1: Merge sections ---- */
    msec_cap = 16;
    msecs = (struct merged_section *)xcalloc_ld(
        (unsigned long)msec_cap, sizeof(struct merged_section));
    num_msecs = 0;

    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_sections; j++) {
            struct section *sec = &objs[i].sections[j];
            int mi;
            u64 sec_align;
            u64 cur_size;
            u64 aligned_off;

            if (sec->name == NULL || sec->name[0] == '\0') {
                continue;
            }
            /* skip symtab, strtab, rela sections (we rebuild them) */
            if (sec->shdr.sh_type == SHT_SYMTAB ||
                sec->shdr.sh_type == SHT_STRTAB ||
                sec->shdr.sh_type == SHT_RELA) {
                continue;
            }
            if (sec->shdr.sh_type == SHT_NULL) {
                continue;
            }
            if (sec->shdr.sh_size == 0 && sec->shdr.sh_type != SHT_NOBITS) {
                continue;
            }

            /* find or create merged section */
            mi = -1;
            for (k = 0; k < num_msecs; k++) {
                if (strcmp(msecs[k].name, sec->name) == 0) {
                    mi = k;
                    break;
                }
            }
            if (mi < 0) {
                if (num_msecs >= msec_cap) {
                    msec_cap *= 2;
                    msecs = (struct merged_section *)realloc(msecs,
                        (unsigned long)msec_cap *
                        sizeof(struct merged_section));
                }
                mi = num_msecs++;
                memset(&msecs[mi], 0, sizeof(struct merged_section));
                msecs[mi].name = sec->name;
                msecs[mi].shdr.sh_type = sec->shdr.sh_type;
                msecs[mi].shdr.sh_flags = sec->shdr.sh_flags;
                msecs[mi].shdr.sh_addralign = sec->shdr.sh_addralign;
            }

            sec_align = sec->shdr.sh_addralign;
            if (sec_align < 1) {
                sec_align = 1;
            }
            if (sec_align > msecs[mi].shdr.sh_addralign) {
                msecs[mi].shdr.sh_addralign = sec_align;
            }

            cur_size = msecs[mi].size;
            aligned_off = (cur_size + (unsigned long)sec_align - 1) &
                          ~((unsigned long)sec_align - 1);

            /* record input piece */
            if (msecs[mi].num_inputs >= msecs[mi].input_cap) {
                int nc = msecs[mi].input_cap == 0
                    ? 16 : msecs[mi].input_cap * 2;
                msecs[mi].inputs = (struct input_piece *)realloc(
                    msecs[mi].inputs,
                    (unsigned long)nc * sizeof(struct input_piece));
                msecs[mi].input_cap = nc;
            }
            {
                struct input_piece *ip =
                    &msecs[mi].inputs[msecs[mi].num_inputs++];
                ip->obj_idx = i;
                ip->sec_idx = j;
                ip->offset_in_merged = aligned_off;
            }

            /* record the output section index for this input section */
            sec->out_sec_idx = mi;

            if (sec->shdr.sh_type != SHT_NOBITS && sec->data) {
                unsigned long new_size;
                new_size = (unsigned long)(aligned_off + sec->shdr.sh_size);
                if (new_size > msecs[mi].capacity) {
                    unsigned long nc;
                    nc = msecs[mi].capacity == 0
                        ? 4096 : msecs[mi].capacity;
                    while (nc < new_size) {
                        nc *= 2;
                    }
                    msecs[mi].data = (u8 *)realloc(msecs[mi].data, nc);
                    if (nc > msecs[mi].capacity) {
                        memset(msecs[mi].data + msecs[mi].capacity, 0,
                               nc - msecs[mi].capacity);
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
            } else if (sec->shdr.sh_type == SHT_NOBITS) {
                unsigned long new_size;
                new_size = (unsigned long)(aligned_off + sec->shdr.sh_size);
                if (new_size > msecs[mi].size) {
                    msecs[mi].size = new_size;
                }
            }
        }
    }

    /* ---- Phase 2: Build merged symbol table ---- */
    strtab_cap = 1024;
    strtab = (u8 *)xmalloc(strtab_cap);
    strtab[0] = '\0';
    strtab_size = 1;

    out_sym_cap = 256;
    out_syms = (Elf64_Sym *)xcalloc_ld(
        (unsigned long)out_sym_cap, sizeof(Elf64_Sym));
    out_nsyms = 1; /* entry 0 is null symbol */

    sym_map = (int **)xcalloc_ld(
        (unsigned long)num_objs, sizeof(int *));
    for (i = 0; i < num_objs; i++) {
        sym_map[i] = (int *)xcalloc_ld(
            (unsigned long)objs[i].num_symbols, sizeof(int));
    }

    /* first pass: local symbols */
    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_symbols; j++) {
            Elf64_Sym *sym = &objs[i].symbols[j].sym;
            int bind = ELF64_ST_BIND(sym->st_info);
            int sec_idx;
            int out_sec;
            Elf64_Sym *os;
            u32 name_off;
            u32 name_len;

            if (bind != STB_LOCAL) {
                continue;
            }
            if (j == 0) {
                sym_map[i][j] = 0;
                continue;
            }

            /* compute new section index */
            if (sym->st_shndx == SHN_UNDEF || sym->st_shndx == SHN_ABS ||
                sym->st_shndx == SHN_COMMON) {
                out_sec = (int)sym->st_shndx;
            } else {
                sec_idx = (int)sym->st_shndx;
                if (sec_idx < objs[i].num_sections &&
                    objs[i].sections[sec_idx].out_sec_idx >= 0) {
                    /* +1 because section 0 is null */
                    out_sec = objs[i].sections[sec_idx].out_sec_idx + 1;
                } else {
                    out_sec = SHN_UNDEF;
                }
            }

            /* add name to strtab */
            name_len = (u32)(strlen(objs[i].symbols[j].name) + 1);
            if (strtab_size + name_len > strtab_cap) {
                while (strtab_size + name_len > strtab_cap) {
                    strtab_cap *= 2;
                }
                strtab = (u8 *)realloc(strtab, strtab_cap);
            }
            name_off = strtab_size;
            memcpy(strtab + strtab_size, objs[i].symbols[j].name, name_len);
            strtab_size += name_len;

            /* add symbol */
            if (out_nsyms >= out_sym_cap) {
                out_sym_cap *= 2;
                out_syms = (Elf64_Sym *)realloc(out_syms,
                    (unsigned long)out_sym_cap * sizeof(Elf64_Sym));
            }
            os = &out_syms[out_nsyms];
            memcpy(os, sym, sizeof(Elf64_Sym));
            os->st_name = name_off;
            os->st_shndx = (u16)out_sec;

            /* adjust st_value for section-relative offset in merged section */
            if (out_sec != SHN_UNDEF && out_sec != (int)(u16)SHN_ABS &&
                out_sec != (int)(u16)SHN_COMMON &&
                out_sec > 0 && (out_sec - 1) < num_msecs) {
                int mi = out_sec - 1;
                int sec_idx2 = (int)sym->st_shndx;
                /* find offset of this input in the merged section */
                for (k = 0; k < msecs[mi].num_inputs; k++) {
                    if (msecs[mi].inputs[k].obj_idx == i &&
                        msecs[mi].inputs[k].sec_idx == sec_idx2) {
                        os->st_value = sym->st_value +
                                       msecs[mi].inputs[k].offset_in_merged;
                        break;
                    }
                }
            }

            sym_map[i][j] = out_nsyms;
            out_nsyms++;
        }
    }

    /* record first global index for sh_info */
    {
        int first_global = out_nsyms;

        /* second pass: global and weak symbols */
        for (i = 0; i < num_objs; i++) {
            for (j = 0; j < objs[i].num_symbols; j++) {
                Elf64_Sym *sym = &objs[i].symbols[j].sym;
                int bind = ELF64_ST_BIND(sym->st_info);
                int sec_idx;
                int out_sec;
                Elf64_Sym *os;
                u32 name_off;
                u32 name_len;
                int existing;

                if (bind != STB_GLOBAL && bind != STB_WEAK) {
                    continue;
                }
                if (objs[i].symbols[j].name[0] == '\0') {
                    sym_map[i][j] = 0;
                    continue;
                }

                /* check for duplicate: if already in output, map to it */
                existing = -1;
                {
                    int s;
                    for (s = first_global; s < out_nsyms; s++) {
                        const char *ename;
                        ename = (const char *)(strtab + out_syms[s].st_name);
                        if (strcmp(ename, objs[i].symbols[j].name) == 0) {
                            existing = s;
                            break;
                        }
                    }
                }
                if (existing >= 0) {
                    /* if existing is undefined and this is defined, replace */
                    if (out_syms[existing].st_shndx == SHN_UNDEF &&
                        sym->st_shndx != SHN_UNDEF) {
                        /* update it */
                        sec_idx = (int)sym->st_shndx;
                        if (sec_idx < objs[i].num_sections &&
                            objs[i].sections[sec_idx].out_sec_idx >= 0) {
                            out_sec = objs[i].sections[sec_idx].out_sec_idx + 1;
                        } else {
                            out_sec = SHN_UNDEF;
                        }
                        out_syms[existing].st_shndx = (u16)out_sec;
                        out_syms[existing].st_info = sym->st_info;
                        out_syms[existing].st_other = sym->st_other;
                        out_syms[existing].st_size = sym->st_size;

                        if (out_sec > 0 && (out_sec - 1) < num_msecs) {
                            int mi = out_sec - 1;
                            for (k = 0; k < msecs[mi].num_inputs; k++) {
                                if (msecs[mi].inputs[k].obj_idx == i &&
                                    msecs[mi].inputs[k].sec_idx == sec_idx) {
                                    out_syms[existing].st_value =
                                        sym->st_value +
                                        msecs[mi].inputs[k].offset_in_merged;
                                    break;
                                }
                            }
                        } else {
                            out_syms[existing].st_value = sym->st_value;
                        }
                    }
                    sym_map[i][j] = existing;
                    continue;
                }

                /* compute new section index */
                if (sym->st_shndx == SHN_UNDEF || sym->st_shndx == SHN_ABS ||
                    sym->st_shndx == SHN_COMMON) {
                    out_sec = (int)sym->st_shndx;
                } else {
                    sec_idx = (int)sym->st_shndx;
                    if (sec_idx < objs[i].num_sections &&
                        objs[i].sections[sec_idx].out_sec_idx >= 0) {
                        out_sec = objs[i].sections[sec_idx].out_sec_idx + 1;
                    } else {
                        out_sec = SHN_UNDEF;
                    }
                }

                /* add name to strtab */
                name_len = (u32)(strlen(objs[i].symbols[j].name) + 1);
                if (strtab_size + name_len > strtab_cap) {
                    while (strtab_size + name_len > strtab_cap) {
                        strtab_cap *= 2;
                    }
                    strtab = (u8 *)realloc(strtab, strtab_cap);
                }
                name_off = strtab_size;
                memcpy(strtab + strtab_size,
                       objs[i].symbols[j].name, name_len);
                strtab_size += name_len;

                /* add symbol */
                if (out_nsyms >= out_sym_cap) {
                    out_sym_cap *= 2;
                    out_syms = (Elf64_Sym *)realloc(out_syms,
                        (unsigned long)out_sym_cap * sizeof(Elf64_Sym));
                }
                os = &out_syms[out_nsyms];
                memcpy(os, sym, sizeof(Elf64_Sym));
                os->st_name = name_off;
                os->st_shndx = (u16)out_sec;

                if (out_sec != SHN_UNDEF &&
                    out_sec != (int)(u16)SHN_ABS &&
                    out_sec != (int)(u16)SHN_COMMON &&
                    out_sec > 0 && (out_sec - 1) < num_msecs) {
                    int mi = out_sec - 1;
                    int sec_idx2 = (int)sym->st_shndx;
                    for (k = 0; k < msecs[mi].num_inputs; k++) {
                        if (msecs[mi].inputs[k].obj_idx == i &&
                            msecs[mi].inputs[k].sec_idx == sec_idx2) {
                            os->st_value = sym->st_value +
                                           msecs[mi].inputs[k].offset_in_merged;
                            break;
                        }
                    }
                }

                sym_map[i][j] = out_nsyms;
                out_nsyms++;
            }
        }

    /* ---- Phase 3: Rewrite relocations ---- */

    /*
     * Group relocations by their target output section.
     * For each rela section in the input, map the target section to
     * the output section index, adjust r_offset by the input piece's
     * offset in the merged section, and update r_info with the new
     * symbol index from sym_map.
     */
    out_rela_cap = 256;
    out_relas = (Elf64_Rela *)xcalloc_ld(
        (unsigned long)out_rela_cap, sizeof(Elf64_Rela));
    rela_target_sec = (int *)xcalloc_ld(
        (unsigned long)out_rela_cap, sizeof(int));
    out_nrelas = 0;

    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_relas; j++) {
            struct elf_rela *r = &objs[i].relas[j];
            int old_sec = r->target_section;
            int new_sec;
            int old_sym;
            int new_sym;
            u32 rtype;
            Elf64_Rela *out_r;
            u64 offset_adj;

            if (old_sec < 0 || old_sec >= objs[i].num_sections) {
                continue;
            }
            if (objs[i].sections[old_sec].out_sec_idx < 0) {
                continue;
            }

            new_sec = objs[i].sections[old_sec].out_sec_idx + 1;

            old_sym = (int)ELF64_R_SYM(r->rela.r_info);
            rtype = (u32)ELF64_R_TYPE(r->rela.r_info);

            if (old_sym < 0 || old_sym >= objs[i].num_symbols) {
                new_sym = 0;
            } else {
                new_sym = sym_map[i][old_sym];
            }

            /* compute offset adjustment */
            offset_adj = 0;
            {
                int mi = new_sec - 1;
                for (k = 0; k < msecs[mi].num_inputs; k++) {
                    if (msecs[mi].inputs[k].obj_idx == i &&
                        msecs[mi].inputs[k].sec_idx == old_sec) {
                        offset_adj = msecs[mi].inputs[k].offset_in_merged;
                        break;
                    }
                }
            }

            if (out_nrelas >= out_rela_cap) {
                out_rela_cap *= 2;
                out_relas = (Elf64_Rela *)realloc(out_relas,
                    (unsigned long)out_rela_cap * sizeof(Elf64_Rela));
                rela_target_sec = (int *)realloc(rela_target_sec,
                    (unsigned long)out_rela_cap * sizeof(int));
            }
            out_r = &out_relas[out_nrelas];
            out_r->r_offset = r->rela.r_offset + offset_adj;
            out_r->r_info = ELF64_R_INFO((u64)new_sym, rtype);
            out_r->r_addend = r->rela.r_addend;
            rela_target_sec[out_nrelas] = new_sec;
            out_nrelas++;
        }
    }

    /* ---- Phase 4: Build section headers and write output ---- */

    /* collect unique rela target sections */
    {
        int *rela_secs;  /* unique output section indices that have relas */
        int nrela_secs;
        int rela_sec_cap;

        rela_secs = (int *)xcalloc_ld(64, sizeof(int));
        nrela_secs = 0;
        rela_sec_cap = 64;

        for (i = 0; i < out_nrelas; i++) {
            int found = 0;
            for (k = 0; k < nrela_secs; k++) {
                if (rela_secs[k] == rela_target_sec[i]) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (nrela_secs >= rela_sec_cap) {
                    rela_sec_cap *= 2;
                    rela_secs = (int *)realloc(rela_secs,
                        (unsigned long)rela_sec_cap * sizeof(int));
                }
                rela_secs[nrela_secs++] = rela_target_sec[i];
            }
        }

        /*
         * Output section layout:
         *   [0] null
         *   [1..num_msecs] merged data sections
         *   [num_msecs+1..num_msecs+nrela_secs] rela sections
         *   [num_msecs+nrela_secs+1] .symtab
         *   [num_msecs+nrela_secs+2] .strtab
         *   [num_msecs+nrela_secs+3] .shstrtab
         */
        num_out_secs = 1 + num_msecs + nrela_secs + 3;
        symtab_idx = 1 + num_msecs + nrela_secs;
        strtab_idx = symtab_idx + 1;
        shstrtab_idx = strtab_idx + 1;

        out_shdrs = (Elf64_Shdr *)xcalloc_ld(
            (unsigned long)num_out_secs, sizeof(Elf64_Shdr));

        /* build shstrtab */
        shstrtab_cap = 512;
        shstrtab = (u8 *)xmalloc(shstrtab_cap);
        shstrtab[0] = '\0';
        shstrtab_size = 1;

        /* helper: add name to shstrtab */
        #define ADD_SHNAME(name_str, off_var) do { \
            u32 _len = (u32)(strlen(name_str) + 1); \
            if (shstrtab_size + _len > shstrtab_cap) { \
                while (shstrtab_size + _len > shstrtab_cap) \
                    shstrtab_cap *= 2; \
                shstrtab = (u8 *)realloc(shstrtab, shstrtab_cap); \
            } \
            (off_var) = shstrtab_size; \
            memcpy(shstrtab + shstrtab_size, (name_str), _len); \
            shstrtab_size += _len; \
        } while (0)

        /* assign section header 0: null */
        memset(&out_shdrs[0], 0, sizeof(Elf64_Shdr));

        /* assign file offsets */
        file_offset = sizeof(Elf64_Ehdr); /* no program headers for ET_REL */

        /* data sections */
        for (i = 0; i < num_msecs; i++) {
            u32 nm;
            u64 align;
            ADD_SHNAME(msecs[i].name, nm);
            out_shdrs[i + 1].sh_name = nm;
            out_shdrs[i + 1].sh_type = msecs[i].shdr.sh_type;
            out_shdrs[i + 1].sh_flags = msecs[i].shdr.sh_flags;
            out_shdrs[i + 1].sh_addr = 0;
            align = msecs[i].shdr.sh_addralign;
            if (align < 1) align = 1;
            file_offset = (file_offset + align - 1) & ~(align - 1);
            out_shdrs[i + 1].sh_offset = file_offset;
            out_shdrs[i + 1].sh_size = msecs[i].size;
            out_shdrs[i + 1].sh_addralign = align;
            if (msecs[i].shdr.sh_type != SHT_NOBITS) {
                file_offset += msecs[i].size;
            }
        }

        /* rela sections */
        for (i = 0; i < nrela_secs; i++) {
            int sec_out_idx = 1 + num_msecs + i;
            char rela_name[256];
            u32 nm;
            int count;

            /* find name of target section */
            sprintf(rela_name, ".rela%s", msecs[rela_secs[i] - 1].name);

            ADD_SHNAME(rela_name, nm);

            /* count relas for this section */
            count = 0;
            for (k = 0; k < out_nrelas; k++) {
                if (rela_target_sec[k] == rela_secs[i]) {
                    count++;
                }
            }

            file_offset = (file_offset + 7) & ~(u64)7;
            out_shdrs[sec_out_idx].sh_name = nm;
            out_shdrs[sec_out_idx].sh_type = SHT_RELA;
            out_shdrs[sec_out_idx].sh_flags = SHF_INFO_LINK;
            out_shdrs[sec_out_idx].sh_offset = file_offset;
            out_shdrs[sec_out_idx].sh_size =
                (u64)count * sizeof(Elf64_Rela);
            out_shdrs[sec_out_idx].sh_link = (u32)symtab_idx;
            out_shdrs[sec_out_idx].sh_info = (u32)rela_secs[i];
            out_shdrs[sec_out_idx].sh_addralign = 8;
            out_shdrs[sec_out_idx].sh_entsize = sizeof(Elf64_Rela);
            file_offset += out_shdrs[sec_out_idx].sh_size;
        }

        /* .symtab */
        {
            u32 nm;
            ADD_SHNAME(".symtab", nm);
            file_offset = (file_offset + 7) & ~(u64)7;
            out_shdrs[symtab_idx].sh_name = nm;
            out_shdrs[symtab_idx].sh_type = SHT_SYMTAB;
            out_shdrs[symtab_idx].sh_offset = file_offset;
            out_shdrs[symtab_idx].sh_size =
                (u64)out_nsyms * sizeof(Elf64_Sym);
            out_shdrs[symtab_idx].sh_link = (u32)strtab_idx;
            out_shdrs[symtab_idx].sh_info = (u32)first_global;
            out_shdrs[symtab_idx].sh_addralign = 8;
            out_shdrs[symtab_idx].sh_entsize = sizeof(Elf64_Sym);
            file_offset += out_shdrs[symtab_idx].sh_size;
        }

        /* .strtab */
        {
            u32 nm;
            ADD_SHNAME(".strtab", nm);
            out_shdrs[strtab_idx].sh_name = nm;
            out_shdrs[strtab_idx].sh_type = SHT_STRTAB;
            out_shdrs[strtab_idx].sh_offset = file_offset;
            out_shdrs[strtab_idx].sh_size = strtab_size;
            out_shdrs[strtab_idx].sh_addralign = 1;
            file_offset += strtab_size;
        }

        /* .shstrtab */
        {
            u32 nm;
            ADD_SHNAME(".shstrtab", nm);
            out_shdrs[shstrtab_idx].sh_name = nm;
            out_shdrs[shstrtab_idx].sh_type = SHT_STRTAB;
            out_shdrs[shstrtab_idx].sh_offset = file_offset;
            out_shdrs[shstrtab_idx].sh_size = shstrtab_size;
            out_shdrs[shstrtab_idx].sh_addralign = 1;
            file_offset += shstrtab_size;
        }

        /* section header table offset */
        file_offset = (file_offset + 7) & ~(u64)7;

        /* build ELF header */
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
        ehdr.e_phoff = 0;  /* no program headers for ET_REL */
        ehdr.e_shoff = file_offset;
        ehdr.e_flags = 0;
        ehdr.e_ehsize = sizeof(Elf64_Ehdr);
        ehdr.e_phentsize = 0;
        ehdr.e_phnum = 0;
        ehdr.e_shentsize = sizeof(Elf64_Shdr);
        ehdr.e_shnum = (u16)num_out_secs;
        ehdr.e_shstrndx = (u16)shstrtab_idx;

        /* ---- Write the output ---- */
        f = fopen(output, "wb");
        if (!f) {
            fprintf(stderr, "ld: cannot create '%s'\n", output);
            exit(1);
        }

        /* ELF header */
        fwrite(&ehdr, sizeof(Elf64_Ehdr), 1, f);

        /* section data */
        {
            unsigned long pos = sizeof(Elf64_Ehdr);

            /* data sections */
            for (i = 0; i < num_msecs; i++) {
                unsigned long off;
                off = (unsigned long)out_shdrs[i + 1].sh_offset;
                if (off > pos) {
                    unsigned long pad = off - pos;
                    unsigned long p;
                    for (p = 0; p < pad; p++) {
                        fputc(0, f);
                    }
                    pos = off;
                }
                if (msecs[i].shdr.sh_type != SHT_NOBITS &&
                    msecs[i].data && msecs[i].size > 0) {
                    fwrite(msecs[i].data, 1, msecs[i].size, f);
                    pos += msecs[i].size;
                }
            }

            /* rela sections */
            for (i = 0; i < nrela_secs; i++) {
                int sec_out_idx = 1 + num_msecs + i;
                unsigned long off;
                off = (unsigned long)out_shdrs[sec_out_idx].sh_offset;
                if (off > pos) {
                    unsigned long pad = off - pos;
                    unsigned long p;
                    for (p = 0; p < pad; p++) {
                        fputc(0, f);
                    }
                    pos = off;
                }
                /* write relas for this target section */
                for (k = 0; k < out_nrelas; k++) {
                    if (rela_target_sec[k] == rela_secs[i]) {
                        fwrite(&out_relas[k], sizeof(Elf64_Rela), 1, f);
                        pos += sizeof(Elf64_Rela);
                    }
                }
            }

            /* symtab */
            {
                unsigned long off;
                off = (unsigned long)out_shdrs[symtab_idx].sh_offset;
                if (off > pos) {
                    unsigned long pad = off - pos;
                    unsigned long p;
                    for (p = 0; p < pad; p++) {
                        fputc(0, f);
                    }
                    pos = off;
                }
                fwrite(out_syms, sizeof(Elf64_Sym),
                       (unsigned long)out_nsyms, f);
                pos += (unsigned long)out_nsyms * sizeof(Elf64_Sym);
            }

            /* strtab */
            fwrite(strtab, 1, strtab_size, f);
            pos += strtab_size;

            /* shstrtab */
            fwrite(shstrtab, 1, shstrtab_size, f);
            pos += shstrtab_size;

            /* pad to section headers */
            {
                unsigned long sh_off = (unsigned long)ehdr.e_shoff;
                if (sh_off > pos) {
                    unsigned long pad = sh_off - pos;
                    unsigned long p;
                    for (p = 0; p < pad; p++) {
                        fputc(0, f);
                    }
                }
            }
        }

        /* section headers */
        fwrite(out_shdrs, sizeof(Elf64_Shdr),
               (unsigned long)num_out_secs, f);

        fclose(f);

        /* cleanup */
        free(rela_secs);
        free(out_shdrs);
    }

    #undef ADD_SHNAME

    for (i = 0; i < num_objs; i++) {
        free(sym_map[i]);
    }
    free(sym_map);
    free(out_syms);
    free(strtab);
    free(shstrtab);
    free(out_relas);
    free(rela_target_sec);
    layout_free(msecs, num_msecs);

    } /* end of first_global block */

    return 0;
}

/* ---- main ---- */

/*
 * link_shared - produce ET_DYN shared library.
 * Generates dynamic sections (dynsym, dynstr, hash, got, rela.dyn,
 * dynamic) and a PT_DYNAMIC program header alongside the normal
 * PT_LOAD segments.
 */
static int link_shared(const char *output, const char *soname,
                       struct elf_obj *objs, int num_objs,
                       const char **inputs, int num_inputs)
{
    struct merged_section *msecs;
    int num_msecs;
    Elf64_Phdr phdrs[4]; /* RX, RW, DYNAMIC, (spare) */
    int num_phdrs;
    Elf64_Ehdr ehdr;
    struct out_section *out_secs;
    u32 *name_offsets;
    u8 *shstrtab_data;
    u32 shstrtab_size;
    u64 shstrtab_offset;
    u64 sh_offset;
    int dyn_sec_count;
    int total_secs;
    int i;

    /* dyn section data */
    u8 *dynsym_d;  u32 dynsym_sz;
    u8 *dynstr_d;  u32 dynstr_sz;
    u8 *hash_d;    u32 hash_sz;
    u8 *got_d;     u32 got_sz;
    u8 *rela_d;    u32 rela_sz;
    u8 *dynamic_d; u32 dynamic_sz;

    /* dyn section addresses (assigned after layout) */
    u64 dynsym_addr;
    u64 dynstr_addr;
    u64 hash_addr;
    u64 got_addr;
    u64 rela_addr;
    u64 dynamic_addr;

    u64 max_end;

    (void)inputs;
    (void)num_inputs;

    /* resolve symbols (allow undefined) */
    resolve_symbols_shared(objs, num_objs);

    /* merge and layout user sections */
    layout_sections(objs, num_objs, &msecs, &num_msecs,
                    phdrs, &num_phdrs);

    /* compute symbol addresses */
    compute_symbol_addrs(objs, num_objs, msecs, num_msecs);

    /* apply relocations */
    apply_relocations(objs, num_objs);

    /* copy patched data back into merged sections */
    for (i = 0; i < num_msecs; i++) {
        int j;
        for (j = 0; j < msecs[i].num_inputs; j++) {
            struct input_piece *ip;
            struct section *sec;
            ip = &msecs[i].inputs[j];
            sec = &objs[ip->obj_idx].sections[ip->sec_idx];
            if (sec->shdr.sh_type != SHT_NOBITS && sec->data) {
                memcpy(msecs[i].data + ip->offset_in_merged,
                       sec->data,
                       (unsigned long)sec->shdr.sh_size);
            }
        }
    }

    /* collect global symbols for dynamic export */
    dyn_collect_globals(objs, num_objs);

    /* build dynamic section data (addresses will be patched) */
    dyn_build_sections(0, soname);

    /* get section data pointers */
    dynsym_d = dyn_get_dynsym(&dynsym_sz);
    dynstr_d = dyn_get_dynstr(&dynstr_sz);
    hash_d = dyn_get_hash(&hash_sz);
    got_d = dyn_get_got(&got_sz);
    rela_d = dyn_get_rela_dyn(&rela_sz);
    dynamic_d = dyn_get_dynamic(&dynamic_sz);

    /*
     * Assign file offsets/vaddrs for dynamic sections.
     * Place them after the last merged section data.
     */
    max_end = 0;
    for (i = 0; i < num_msecs; i++) {
        u64 end;
        if (msecs[i].shdr.sh_type == SHT_NOBITS) {
            continue;
        }
        end = msecs[i].shdr.sh_offset + msecs[i].size;
        if (end > max_end) {
            max_end = end;
        }
    }

    /* also check writable section vaddrs */
    {
        u64 max_vend = 0;
        for (i = 0; i < num_msecs; i++) {
            u64 vend = msecs[i].shdr.sh_addr + msecs[i].size;
            if (vend > max_vend) {
                max_vend = vend;
            }
        }
        /* align to 8 */
        max_end = (max_end + 7) & ~(u64)7;
        max_vend = (max_vend + 7) & ~(u64)7;

        /* dynamic sections go in the data segment (RW) */
        dynsym_addr = max_vend;
        dynstr_addr = dynsym_addr + dynsym_sz;
        hash_addr = (dynstr_addr + hash_sz + 7) & ~(u64)7;
        hash_addr = dynstr_addr + dynstr_sz;
        hash_addr = (hash_addr + 3) & ~(u64)3;
        rela_addr = hash_addr + hash_sz;
        rela_addr = (rela_addr + 7) & ~(u64)7;
        got_addr = rela_addr + rela_sz;
        got_addr = (got_addr + 7) & ~(u64)7;
        dynamic_addr = got_addr + got_sz;
        dynamic_addr = (dynamic_addr + 7) & ~(u64)7;
    }

    /* patch dynamic section addresses now that we know them */
    dyn_patch_addrs(dynsym_addr, dynstr_addr, hash_addr,
                    got_addr, rela_addr, dynamic_addr, soname);

    /* re-fetch after patching */
    dynamic_d = dyn_get_dynamic(&dynamic_sz);
    got_d = dyn_get_got(&got_sz);
    rela_d = dyn_get_rela_dyn(&rela_sz);

    /* build output sections: merged + 6 dynamic sections */
    dyn_sec_count = 6;
    total_secs = num_msecs + dyn_sec_count;
    out_secs = (struct out_section *)calloc(
        (unsigned long)total_secs, sizeof(struct out_section));

    for (i = 0; i < num_msecs; i++) {
        memcpy(&out_secs[i].shdr, &msecs[i].shdr, sizeof(Elf64_Shdr));
        out_secs[i].data = msecs[i].data;
        out_secs[i].size = msecs[i].size;
    }

    /* compute file offsets for dynamic sections */
    {
        u64 fo = max_end;
        int idx = num_msecs;

        /* .dynsym */
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_DYNSYM;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = dynsym_addr;
        out_secs[idx].shdr.sh_size = dynsym_sz;
        out_secs[idx].shdr.sh_entsize = sizeof(Elf64_Sym);
        out_secs[idx].shdr.sh_addralign = 8;
        out_secs[idx].shdr.sh_link = (u32)(idx + 1);
        out_secs[idx].shdr.sh_info = 1;
        out_secs[idx].data = dynsym_d;
        out_secs[idx].size = dynsym_sz;
        fo += dynsym_sz;
        idx++;

        /* .dynstr */
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_STRTAB;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = dynstr_addr;
        out_secs[idx].shdr.sh_size = dynstr_sz;
        out_secs[idx].shdr.sh_addralign = 1;
        out_secs[idx].data = dynstr_d;
        out_secs[idx].size = dynstr_sz;
        fo += dynstr_sz;
        idx++;

        /* .hash */
        fo = (fo + 3) & ~(u64)3;
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_HASH;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = hash_addr;
        out_secs[idx].shdr.sh_size = hash_sz;
        out_secs[idx].shdr.sh_entsize = 4;
        out_secs[idx].shdr.sh_addralign = 4;
        out_secs[idx].shdr.sh_link = (u32)(num_msecs);
        out_secs[idx].data = hash_d;
        out_secs[idx].size = hash_sz;
        fo += hash_sz;
        idx++;

        /* .rela.dyn */
        fo = (fo + 7) & ~(u64)7;
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_RELA;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = rela_addr;
        out_secs[idx].shdr.sh_size = rela_sz;
        out_secs[idx].shdr.sh_entsize = sizeof(Elf64_Rela);
        out_secs[idx].shdr.sh_addralign = 8;
        out_secs[idx].shdr.sh_link = (u32)(num_msecs);
        out_secs[idx].data = rela_d;
        out_secs[idx].size = rela_sz;
        fo += rela_sz;
        idx++;

        /* .got */
        fo = (fo + 7) & ~(u64)7;
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_PROGBITS;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC | SHF_WRITE;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = got_addr;
        out_secs[idx].shdr.sh_size = got_sz;
        out_secs[idx].shdr.sh_entsize = 8;
        out_secs[idx].shdr.sh_addralign = 8;
        out_secs[idx].data = got_d;
        out_secs[idx].size = got_sz;
        fo += got_sz;
        idx++;

        /* .dynamic */
        fo = (fo + 7) & ~(u64)7;
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_DYNAMIC;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC | SHF_WRITE;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = dynamic_addr;
        out_secs[idx].shdr.sh_size = dynamic_sz;
        out_secs[idx].shdr.sh_entsize = sizeof(Elf64_Dyn);
        out_secs[idx].shdr.sh_addralign = 8;
        out_secs[idx].shdr.sh_link = (u32)(num_msecs + 1);
        out_secs[idx].data = dynamic_d;
        out_secs[idx].size = dynamic_sz;

        (void)fo;
    }

    /* add PT_DYNAMIC program header */
    {
        int di = num_phdrs;
        memset(&phdrs[di], 0, sizeof(Elf64_Phdr));
        phdrs[di].p_type = PT_DYNAMIC;
        phdrs[di].p_flags = PF_R | PF_W;
        phdrs[di].p_offset =
            out_secs[num_msecs + 5].shdr.sh_offset;
        phdrs[di].p_vaddr = dynamic_addr;
        phdrs[di].p_paddr = dynamic_addr;
        phdrs[di].p_filesz = dynamic_sz;
        phdrs[di].p_memsz = dynamic_sz;
        phdrs[di].p_align = 8;
        num_phdrs = di + 1;
    }

    /* build shstrtab */
    name_offsets = (u32 *)calloc(
        (unsigned long)total_secs, sizeof(u32));

    /* we need a custom shstrtab that includes dynamic section names */
    {
        static const char *dyn_names[] = {
            ".dynsym", ".dynstr", ".hash",
            ".rela.dyn", ".got", ".dynamic"
        };
        u32 cap = 512;
        u32 pos = 0;
        int j;

        shstrtab_data = (u8 *)malloc(cap);

        /* null byte */
        shstrtab_data[pos++] = '\0';

        /* ".shstrtab" */
        {
            const char *s = ".shstrtab";
            u32 len = (u32)(strlen(s) + 1);
            memcpy(shstrtab_data + pos, s, len);
            pos += len;
        }

        /* merged section names */
        for (j = 0; j < num_msecs; j++) {
            u32 len = (u32)(strlen(msecs[j].name) + 1);
            if (pos + len > cap) {
                while (pos + len > cap) cap *= 2;
                shstrtab_data = (u8 *)realloc(
                    shstrtab_data, cap);
            }
            name_offsets[j] = pos;
            memcpy(shstrtab_data + pos, msecs[j].name, len);
            pos += len;
        }

        /* dynamic section names */
        for (j = 0; j < dyn_sec_count; j++) {
            u32 len = (u32)(strlen(dyn_names[j]) + 1);
            if (pos + len > cap) {
                while (pos + len > cap) cap *= 2;
                shstrtab_data = (u8 *)realloc(
                    shstrtab_data, cap);
            }
            name_offsets[num_msecs + j] = pos;
            memcpy(shstrtab_data + pos, dyn_names[j], len);
            pos += len;
        }

        shstrtab_size = pos;
    }

    /* assign sh_name to all output sections */
    for (i = 0; i < total_secs; i++) {
        out_secs[i].shdr.sh_name = name_offsets[i];
    }

    /* compute shstrtab file offset */
    {
        u64 end = 0;
        for (i = 0; i < total_secs; i++) {
            u64 e;
            if (out_secs[i].shdr.sh_type == SHT_NOBITS) {
                continue;
            }
            e = out_secs[i].shdr.sh_offset + out_secs[i].size;
            if (e > end) {
                end = e;
            }
        }
        shstrtab_offset = end;
    }

    sh_offset = shstrtab_offset + (u64)shstrtab_size;
    sh_offset = (sh_offset + 7) & ~(u64)7;

    /* build ELF header */
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_ident[7] = ELFOSABI_NONE;
    ehdr.e_type = ET_DYN;
    ehdr.e_machine = EM_AARCH64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = 0;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_shoff = sh_offset;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = (u16)num_phdrs;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = (u16)(total_secs + 2);
    ehdr.e_shstrndx = (u16)(total_secs + 1);

    /* write output */
    elf_write_exec(output, &ehdr, phdrs, num_phdrs,
                   out_secs, total_secs,
                   shstrtab_offset, shstrtab_data, shstrtab_size);

    /* cleanup */
    free(out_secs);
    free(name_offsets);
    free(shstrtab_data);
    dyn_cleanup();
    reloc_cleanup();
    layout_free(msecs, num_msecs);

    return 0;
}

/*
 * link_pie - produce a position-independent executable (ET_DYN with entry).
 * Like a shared library but with an entry point and R_AARCH64_RELATIVE
 * relocations for absolute addresses (used by KASLR).
 */
static int link_pie(const char *output, const char *entry_name_arg,
                    struct elf_obj *objs, int num_objs,
                    struct ld_options *opts)
{
    struct merged_section *msecs;
    int num_msecs;
    Elf64_Phdr phdrs[8]; /* RX, RW, DYNAMIC, GNU_STACK, GNU_RELRO, spare */
    int num_phdrs;
    Elf64_Ehdr ehdr;
    struct out_section *out_secs;
    u32 *name_offsets;
    u8 *shstrtab_data;
    u32 shstrtab_size;
    u64 shstrtab_offset;
    u64 sh_offset;
    u64 entry_addr;
    int total_secs;
    int dyn_sec_count;
    int i;

    /* dyn section data */
    u8 *dynsym_d;  u32 dynsym_sz;
    u8 *dynstr_d;  u32 dynstr_sz;
    u8 *hash_d;    u32 hash_sz;
    u8 *got_d;     u32 got_sz;
    u8 *rela_d;    u32 rela_sz;
    u8 *dynamic_d; u32 dynamic_sz;

    u64 dynsym_addr, dynstr_addr, hash_addr;
    u64 got_addr, rela_addr, dynamic_addr;
    u64 max_end;

    /* resolve symbols */
    resolve_symbols(objs, num_objs);

    /* add defsyms */
    for (i = 0; i < opts->num_defsyms; i++) {
        add_defsym(opts->defsym_names[i], opts->defsym_values[i]);
    }

    /* merge and layout user sections */
    layout_sections(objs, num_objs, &msecs, &num_msecs,
                    phdrs, &num_phdrs);

    /* compute symbol addresses */
    compute_symbol_addrs(objs, num_objs, msecs, num_msecs);

    /* apply relocations */
    apply_relocations(objs, num_objs);

    /* copy patched data back into merged sections */
    for (i = 0; i < num_msecs; i++) {
        int j;
        for (j = 0; j < msecs[i].num_inputs; j++) {
            struct input_piece *ip;
            struct section *sec;
            ip = &msecs[i].inputs[j];
            sec = &objs[ip->obj_idx].sections[ip->sec_idx];
            if (sec->shdr.sh_type != SHT_NOBITS && sec->data) {
                memcpy(msecs[i].data + ip->offset_in_merged,
                       sec->data,
                       (unsigned long)sec->shdr.sh_size);
            }
        }
    }

    /* find entry point */
    entry_addr = find_entry_symbol(entry_name_arg);

    /* collect global symbols for dynamic export */
    dyn_collect_globals(objs, num_objs);

    /* set BIND_NOW if -z now */
    if (opts->z_now) {
        dyn_set_bind_now(1);
    }

    /* build dynamic section data */
    dyn_build_sections(0, NULL);

    /* get section data pointers */
    dynsym_d = dyn_get_dynsym(&dynsym_sz);
    dynstr_d = dyn_get_dynstr(&dynstr_sz);
    hash_d = dyn_get_hash(&hash_sz);
    got_d = dyn_get_got(&got_sz);
    rela_d = dyn_get_rela_dyn(&rela_sz);
    dynamic_d = dyn_get_dynamic(&dynamic_sz);

    /* assign addresses for dynamic sections after last merged section */
    max_end = 0;
    for (i = 0; i < num_msecs; i++) {
        u64 end;
        if (msecs[i].shdr.sh_type == SHT_NOBITS) {
            continue;
        }
        end = msecs[i].shdr.sh_offset + msecs[i].size;
        if (end > max_end) {
            max_end = end;
        }
    }

    {
        u64 max_vend = 0;
        for (i = 0; i < num_msecs; i++) {
            u64 vend = msecs[i].shdr.sh_addr + msecs[i].size;
            if (vend > max_vend) {
                max_vend = vend;
            }
        }
        max_end = (max_end + 7) & ~(u64)7;
        max_vend = (max_vend + 7) & ~(u64)7;

        dynsym_addr = max_vend;
        dynstr_addr = dynsym_addr + dynsym_sz;
        hash_addr = dynstr_addr + dynstr_sz;
        hash_addr = (hash_addr + 3) & ~(u64)3;
        rela_addr = hash_addr + hash_sz;
        rela_addr = (rela_addr + 7) & ~(u64)7;
        got_addr = rela_addr + rela_sz;
        got_addr = (got_addr + 7) & ~(u64)7;
        dynamic_addr = got_addr + got_sz;
        dynamic_addr = (dynamic_addr + 7) & ~(u64)7;
    }

    /* patch dynamic section addresses */
    dyn_patch_addrs(dynsym_addr, dynstr_addr, hash_addr,
                    got_addr, rela_addr, dynamic_addr, NULL);

    /* re-fetch after patching */
    dynamic_d = dyn_get_dynamic(&dynamic_sz);
    got_d = dyn_get_got(&got_sz);
    rela_d = dyn_get_rela_dyn(&rela_sz);

    /* build output sections: merged + 6 dynamic sections */
    dyn_sec_count = 6;
    total_secs = num_msecs + dyn_sec_count;
    out_secs = (struct out_section *)calloc(
        (unsigned long)total_secs, sizeof(struct out_section));

    for (i = 0; i < num_msecs; i++) {
        memcpy(&out_secs[i].shdr, &msecs[i].shdr, sizeof(Elf64_Shdr));
        out_secs[i].data = msecs[i].data;
        out_secs[i].size = msecs[i].size;
    }

    /* dynamic sections file offsets */
    {
        u64 fo = max_end;
        int idx = num_msecs;

        /* .dynsym */
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_DYNSYM;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = dynsym_addr;
        out_secs[idx].shdr.sh_size = dynsym_sz;
        out_secs[idx].shdr.sh_entsize = sizeof(Elf64_Sym);
        out_secs[idx].shdr.sh_addralign = 8;
        out_secs[idx].shdr.sh_link = (u32)(idx + 1);
        out_secs[idx].shdr.sh_info = 1;
        out_secs[idx].data = dynsym_d;
        out_secs[idx].size = dynsym_sz;
        fo += dynsym_sz;
        idx++;

        /* .dynstr */
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_STRTAB;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = dynstr_addr;
        out_secs[idx].shdr.sh_size = dynstr_sz;
        out_secs[idx].shdr.sh_addralign = 1;
        out_secs[idx].data = dynstr_d;
        out_secs[idx].size = dynstr_sz;
        fo += dynstr_sz;
        idx++;

        /* .hash */
        fo = (fo + 3) & ~(u64)3;
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_HASH;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = hash_addr;
        out_secs[idx].shdr.sh_size = hash_sz;
        out_secs[idx].shdr.sh_entsize = 4;
        out_secs[idx].shdr.sh_addralign = 4;
        out_secs[idx].shdr.sh_link = (u32)(num_msecs);
        out_secs[idx].data = hash_d;
        out_secs[idx].size = hash_sz;
        fo += hash_sz;
        idx++;

        /* .rela.dyn */
        fo = (fo + 7) & ~(u64)7;
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_RELA;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = rela_addr;
        out_secs[idx].shdr.sh_size = rela_sz;
        out_secs[idx].shdr.sh_entsize = sizeof(Elf64_Rela);
        out_secs[idx].shdr.sh_addralign = 8;
        out_secs[idx].shdr.sh_link = (u32)(num_msecs);
        out_secs[idx].data = rela_d;
        out_secs[idx].size = rela_sz;
        fo += rela_sz;
        idx++;

        /* .got */
        fo = (fo + 7) & ~(u64)7;
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_PROGBITS;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC | SHF_WRITE;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = got_addr;
        out_secs[idx].shdr.sh_size = got_sz;
        out_secs[idx].shdr.sh_entsize = 8;
        out_secs[idx].shdr.sh_addralign = 8;
        out_secs[idx].data = got_d;
        out_secs[idx].size = got_sz;
        fo += got_sz;
        idx++;

        /* .dynamic */
        fo = (fo + 7) & ~(u64)7;
        memset(&out_secs[idx].shdr, 0, sizeof(Elf64_Shdr));
        out_secs[idx].shdr.sh_type = SHT_DYNAMIC;
        out_secs[idx].shdr.sh_flags = SHF_ALLOC | SHF_WRITE;
        out_secs[idx].shdr.sh_offset = fo;
        out_secs[idx].shdr.sh_addr = dynamic_addr;
        out_secs[idx].shdr.sh_size = dynamic_sz;
        out_secs[idx].shdr.sh_entsize = sizeof(Elf64_Dyn);
        out_secs[idx].shdr.sh_addralign = 8;
        out_secs[idx].shdr.sh_link = (u32)(num_msecs + 1);
        out_secs[idx].data = dynamic_d;
        out_secs[idx].size = dynamic_sz;

        (void)fo;
    }

    /* program headers */
    num_phdrs = 0;

    /* PT_LOAD RX (text segment) */
    phdrs[num_phdrs] = phdrs[0]; /* copy from layout */
    num_phdrs++;

    /* PT_LOAD RW (data segment) - find existing */
    {
        u64 ds_start = 0;
        u64 ds_fstart = 0;
        u64 ds_fsz = 0;
        u64 ds_msz = 0;
        int has_data = 0;

        for (i = 0; i < num_msecs; i++) {
            if (msecs[i].shdr.sh_flags & SHF_WRITE) {
                if (!has_data) {
                    ds_start = msecs[i].shdr.sh_addr;
                    ds_fstart = msecs[i].shdr.sh_offset;
                    has_data = 1;
                }
                if (msecs[i].shdr.sh_type != SHT_NOBITS) {
                    ds_fsz = msecs[i].shdr.sh_offset + msecs[i].size -
                             ds_fstart;
                }
                ds_msz = msecs[i].shdr.sh_addr + msecs[i].size - ds_start;
            }
        }
        /* include dynamic sections in data segment */
        {
            u64 dyn_end = dynamic_addr + dynamic_sz;
            if (dyn_end > ds_start + ds_msz) {
                ds_msz = dyn_end - ds_start;
            }
        }
        if (has_data || dynamic_sz > 0) {
            memset(&phdrs[num_phdrs], 0, sizeof(Elf64_Phdr));
            phdrs[num_phdrs].p_type = PT_LOAD;
            phdrs[num_phdrs].p_flags = PF_R | PF_W;
            phdrs[num_phdrs].p_offset = ds_fstart;
            phdrs[num_phdrs].p_vaddr = ds_start;
            phdrs[num_phdrs].p_paddr = ds_start;
            phdrs[num_phdrs].p_filesz = ds_fsz;
            phdrs[num_phdrs].p_memsz = ds_msz;
            phdrs[num_phdrs].p_align = 0x1000;
            num_phdrs++;
        }
    }

    /* PT_DYNAMIC */
    memset(&phdrs[num_phdrs], 0, sizeof(Elf64_Phdr));
    phdrs[num_phdrs].p_type = PT_DYNAMIC;
    phdrs[num_phdrs].p_flags = PF_R | PF_W;
    phdrs[num_phdrs].p_offset =
        out_secs[num_msecs + 5].shdr.sh_offset;
    phdrs[num_phdrs].p_vaddr = dynamic_addr;
    phdrs[num_phdrs].p_paddr = dynamic_addr;
    phdrs[num_phdrs].p_filesz = dynamic_sz;
    phdrs[num_phdrs].p_memsz = dynamic_sz;
    phdrs[num_phdrs].p_align = 8;
    num_phdrs++;

    /* PT_GNU_STACK (non-executable stack) */
    if (opts->z_noexecstack) {
        memset(&phdrs[num_phdrs], 0, sizeof(Elf64_Phdr));
        phdrs[num_phdrs].p_type = PT_GNU_STACK;
        phdrs[num_phdrs].p_flags = PF_R | PF_W;
        phdrs[num_phdrs].p_align = 16;
        num_phdrs++;
    }

    /* PT_GNU_RELRO */
    if (opts->z_relro) {
        /* RELRO covers .dynamic and .got */
        u64 relro_start = got_addr & ~(u64)0xfff;
        u64 relro_end = dynamic_addr + dynamic_sz;
        relro_end = (relro_end + 0xfff) & ~(u64)0xfff;

        memset(&phdrs[num_phdrs], 0, sizeof(Elf64_Phdr));
        phdrs[num_phdrs].p_type = PT_GNU_RELRO;
        phdrs[num_phdrs].p_flags = PF_R;
        phdrs[num_phdrs].p_vaddr = relro_start;
        phdrs[num_phdrs].p_paddr = relro_start;
        phdrs[num_phdrs].p_memsz = relro_end - relro_start;
        phdrs[num_phdrs].p_filesz = phdrs[num_phdrs].p_memsz;
        /* find file offset for relro_start */
        {
            int si;
            for (si = 0; si < total_secs; si++) {
                if (out_secs[si].shdr.sh_addr <= relro_start &&
                    out_secs[si].shdr.sh_addr + out_secs[si].size >
                    relro_start) {
                    phdrs[num_phdrs].p_offset =
                        out_secs[si].shdr.sh_offset +
                        (relro_start - out_secs[si].shdr.sh_addr);
                    break;
                }
            }
        }
        phdrs[num_phdrs].p_align = 1;
        num_phdrs++;
    }

    /* build shstrtab */
    name_offsets = (u32 *)calloc(
        (unsigned long)total_secs, sizeof(u32));

    {
        static const char *dyn_names[] = {
            ".dynsym", ".dynstr", ".hash",
            ".rela.dyn", ".got", ".dynamic"
        };
        u32 cap = 512;
        u32 pos = 0;
        int j;

        shstrtab_data = (u8 *)malloc(cap);

        shstrtab_data[pos++] = '\0';
        {
            const char *s = ".shstrtab";
            u32 len = (u32)(strlen(s) + 1);
            memcpy(shstrtab_data + pos, s, len);
            pos += len;
        }

        for (j = 0; j < num_msecs; j++) {
            u32 len = (u32)(strlen(msecs[j].name) + 1);
            if (pos + len > cap) {
                while (pos + len > cap) cap *= 2;
                shstrtab_data = (u8 *)realloc(shstrtab_data, cap);
            }
            name_offsets[j] = pos;
            memcpy(shstrtab_data + pos, msecs[j].name, len);
            pos += len;
        }

        for (j = 0; j < dyn_sec_count; j++) {
            u32 len = (u32)(strlen(dyn_names[j]) + 1);
            if (pos + len > cap) {
                while (pos + len > cap) cap *= 2;
                shstrtab_data = (u8 *)realloc(shstrtab_data, cap);
            }
            name_offsets[num_msecs + j] = pos;
            memcpy(shstrtab_data + pos, dyn_names[j], len);
            pos += len;
        }

        shstrtab_size = pos;
    }

    for (i = 0; i < total_secs; i++) {
        out_secs[i].shdr.sh_name = name_offsets[i];
    }

    /* compute shstrtab file offset */
    {
        u64 end = 0;
        for (i = 0; i < total_secs; i++) {
            u64 e;
            if (out_secs[i].shdr.sh_type == SHT_NOBITS) {
                continue;
            }
            e = out_secs[i].shdr.sh_offset + out_secs[i].size;
            if (e > end) {
                end = e;
            }
        }
        shstrtab_offset = end;
    }

    sh_offset = shstrtab_offset + (u64)shstrtab_size;
    sh_offset = (sh_offset + 7) & ~(u64)7;

    /* build ELF header - ET_DYN with entry point */
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_ident[7] = ELFOSABI_NONE;
    ehdr.e_type = ET_DYN;     /* PIE is ET_DYN */
    ehdr.e_machine = EM_AARCH64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = entry_addr; /* but with entry point */
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_shoff = sh_offset;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = (u16)num_phdrs;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = (u16)(total_secs + 2);
    ehdr.e_shstrndx = (u16)(total_secs + 1);

    /* write output */
    elf_write_exec(output, &ehdr, phdrs, num_phdrs,
                   out_secs, total_secs,
                   shstrtab_offset, shstrtab_data, shstrtab_size);

    /* cleanup */
    free(out_secs);
    free(name_offsets);
    free(shstrtab_data);
    dyn_cleanup();
    reloc_cleanup();
    layout_free(msecs, num_msecs);

    return 0;
}

/*
 * parse_version_script - parse a version script file and apply visibility.
 * Simple parser: global { patterns; }; local { *; };
 * For now, just log that we processed it.
 */
static void parse_version_script(const char *path)
{
    FILE *f;
    long len;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ld: cannot open version script '%s'\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fclose(f);

    fprintf(stderr, "ld: processing version script '%s' (%ld bytes)\n",
            path, len);
}

int main(int argc, char **argv)
{
    const char *output;
    const char *entry_name;
    const char *soname;
    const char *script_path;
    const char *map_path;
    const char **inputs;
    int num_inputs;
    int input_cap;
    int shared_mode;
    int relocatable_mode;
    int gc_sections_flag;
    int whole_archive;
    int emit_relocs_flag;
    int build_id_flag;
    int i;
    struct elf_obj *objs;
    struct ld_options opts;
    struct merged_section *msecs;
    int num_msecs;
    Elf64_Phdr phdrs[4];
    int num_phdrs;
    u64 entry_addr;
    Elf64_Ehdr ehdr;
    struct out_section *out_secs;
    u32 *name_offsets;
    u8 *shstrtab_data;
    u32 shstrtab_size;
    u64 shstrtab_offset;
    u64 sh_offset;
    int num_out_secs;
    int in_group;

    output = "a.out";
    entry_name = "_start";
    soname = NULL;
    script_path = NULL;
    map_path = NULL;
    shared_mode = 0;
    relocatable_mode = 0;
    gc_sections_flag = 0;
    whole_archive = 0;
    emit_relocs_flag = 0;
    build_id_flag = 0;
    in_group = 0;
    input_cap = 16;
    inputs = (const char **)malloc((unsigned long)input_cap *
                                   sizeof(const char *));
    num_inputs = 0;

    memset(&opts, 0, sizeof(opts));
    opts.defsym_cap = 8;
    opts.defsym_names = (char **)malloc(
        (unsigned long)opts.defsym_cap * sizeof(char *));
    opts.defsym_values = (u64 *)malloc(
        (unsigned long)opts.defsym_cap * sizeof(u64));

    /* Handle --version / -v before full arg parsing */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 ||
            strcmp(argv[i], "-v") == 0) {
            printf("GNU ld (free-ld) 2.42\n");
            return 0;
        }
    }

    /* parse arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                usage();
            }
            output = argv[++i];
        } else if (strcmp(argv[i], "-e") == 0) {
            if (i + 1 >= argc) {
                usage();
            }
            entry_name = argv[++i];
        } else if (strcmp(argv[i], "-shared") == 0) {
            shared_mode = 1;
        } else if (strcmp(argv[i], "-soname") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            if (i + 1 >= argc) {
                usage();
            }
            soname = argv[++i];
        } else if (strcmp(argv[i], "-T") == 0) {
            if (i + 1 >= argc) {
                usage();
            }
            script_path = argv[++i];
        } else if (strcmp(argv[i], "-r") == 0 ||
                   strcmp(argv[i], "--relocatable") == 0) {
            relocatable_mode = 1;
        } else if (strcmp(argv[i], "--gc-sections") == 0) {
            gc_sections_flag = 1;
        } else if (strcmp(argv[i], "--whole-archive") == 0) {
            whole_archive = 1;
        } else if (strcmp(argv[i], "--no-whole-archive") == 0) {
            whole_archive = 0;
        } else if (strcmp(argv[i], "--emit-relocs") == 0) {
            emit_relocs_flag = 1;
        } else if (strcmp(argv[i], "--build-id") == 0) {
            build_id_flag = 1;
        } else if (strcmp(argv[i], "-Map") == 0) {
            if (i + 1 >= argc) {
                usage();
            }
            map_path = argv[++i];
        } else if (strcmp(argv[i], "-L") == 0) {
            /* -L dir: skip (library search path, not yet used) */
            if (i + 1 >= argc) {
                usage();
            }
            i++;
        } else if (argv[i][0] == '-' && argv[i][1] == 'L') {
            /* -Ldir: skip */
        } else if (strcmp(argv[i], "-l") == 0) {
            /* -l lib: skip (shared library, not yet used) */
            if (i + 1 >= argc) {
                usage();
            }
            i++;
        } else if (argv[i][0] == '-' && argv[i][1] == 'l') {
            /* -llib: skip */
        } else if (strcmp(argv[i], "-pie") == 0) {
            opts.pie = 1;
        } else if (strcmp(argv[i], "-static") == 0) {
            opts.static_mode = 1;
        } else if (strcmp(argv[i], "--no-undefined") == 0 ||
                   strcmp(argv[i], "--no-allow-shlib-undefined") == 0) {
            opts.no_undefined = 1;
        } else if (strcmp(argv[i], "--sort-section=name") == 0) {
            opts.sort_section = 1;
        } else if (strncmp(argv[i], "--sort-section=", 15) == 0) {
            if (strcmp(argv[i] + 15, "name") == 0) {
                opts.sort_section = 1;
            }
        } else if (strcmp(argv[i], "--defsym") == 0) {
            /* --defsym symbol=value */
            char *arg;
            char *eq;
            if (i + 1 >= argc) {
                usage();
            }
            arg = argv[++i];
            eq = strchr(arg, '=');
            if (!eq) {
                fprintf(stderr, "ld: --defsym: expected symbol=value\n");
                exit(1);
            }
            if (opts.num_defsyms >= opts.defsym_cap) {
                opts.defsym_cap *= 2;
                opts.defsym_names = (char **)realloc(opts.defsym_names,
                    (unsigned long)opts.defsym_cap * sizeof(char *));
                opts.defsym_values = (u64 *)realloc(opts.defsym_values,
                    (unsigned long)opts.defsym_cap * sizeof(u64));
            }
            {
                int nlen = (int)(eq - arg);
                char *nm = (char *)xmalloc((unsigned long)(nlen + 1));
                u64 val = 0;
                const char *vp = eq + 1;
                memcpy(nm, arg, (unsigned long)nlen);
                nm[nlen] = '\0';
                /* parse hex or decimal value */
                if (vp[0] == '0' && (vp[1] == 'x' || vp[1] == 'X')) {
                    vp += 2;
                    while (*vp) {
                        char c = *vp++;
                        if (c >= '0' && c <= '9') val = val*16+(u64)(c-'0');
                        else if (c >= 'a' && c <= 'f') val = val*16+(u64)(c-'a'+10);
                        else if (c >= 'A' && c <= 'F') val = val*16+(u64)(c-'A'+10);
                        else break;
                    }
                } else {
                    while (*vp >= '0' && *vp <= '9') {
                        val = val * 10 + (u64)(*vp - '0');
                        vp++;
                    }
                }
                opts.defsym_names[opts.num_defsyms] = nm;
                opts.defsym_values[opts.num_defsyms] = val;
                opts.num_defsyms++;
            }
        } else if (strncmp(argv[i], "--defsym=", 9) == 0) {
            /* --defsym=symbol=value */
            char *arg = argv[i] + 9;
            char *eq = strchr(arg, '=');
            if (!eq) {
                fprintf(stderr, "ld: --defsym: expected symbol=value\n");
                exit(1);
            }
            if (opts.num_defsyms >= opts.defsym_cap) {
                opts.defsym_cap *= 2;
                opts.defsym_names = (char **)realloc(opts.defsym_names,
                    (unsigned long)opts.defsym_cap * sizeof(char *));
                opts.defsym_values = (u64 *)realloc(opts.defsym_values,
                    (unsigned long)opts.defsym_cap * sizeof(u64));
            }
            {
                int nlen = (int)(eq - arg);
                char *nm = (char *)xmalloc((unsigned long)(nlen + 1));
                u64 val = 0;
                const char *vp = eq + 1;
                memcpy(nm, arg, (unsigned long)nlen);
                nm[nlen] = '\0';
                if (vp[0] == '0' && (vp[1] == 'x' || vp[1] == 'X')) {
                    vp += 2;
                    while (*vp) {
                        char c = *vp++;
                        if (c >= '0' && c <= '9') val = val*16+(u64)(c-'0');
                        else if (c >= 'a' && c <= 'f') val = val*16+(u64)(c-'a'+10);
                        else if (c >= 'A' && c <= 'F') val = val*16+(u64)(c-'A'+10);
                        else break;
                    }
                } else {
                    while (*vp >= '0' && *vp <= '9') {
                        val = val * 10 + (u64)(*vp - '0');
                        vp++;
                    }
                }
                opts.defsym_names[opts.num_defsyms] = nm;
                opts.defsym_values[opts.num_defsyms] = val;
                opts.num_defsyms++;
            }
        } else if (strcmp(argv[i], "-z") == 0) {
            /* -z flag */
            if (i + 1 >= argc) {
                usage();
            }
            i++;
            if (strcmp(argv[i], "noexecstack") == 0) {
                opts.z_noexecstack = 1;
            } else if (strcmp(argv[i], "relro") == 0) {
                opts.z_relro = 1;
            } else if (strcmp(argv[i], "now") == 0) {
                opts.z_now = 1;
                opts.z_relro = 1; /* -z now implies -z relro */
            } else if (strncmp(argv[i], "max-page-size=", 14) == 0) {
                u64 val = 0;
                const char *vp = argv[i] + 14;
                if (vp[0] == '0' && (vp[1] == 'x' || vp[1] == 'X')) {
                    vp += 2;
                    while (*vp) {
                        char c = *vp++;
                        if (c >= '0' && c <= '9') val = val*16+(u64)(c-'0');
                        else if (c >= 'a' && c <= 'f') val = val*16+(u64)(c-'a'+10);
                        else if (c >= 'A' && c <= 'F') val = val*16+(u64)(c-'A'+10);
                        else break;
                    }
                } else {
                    while (*vp >= '0' && *vp <= '9') {
                        val = val * 10 + (u64)(*vp - '0');
                        vp++;
                    }
                }
                opts.z_max_page_size = val;
            } else if (strcmp(argv[i], "defs") == 0) {
                opts.no_undefined = 1;
            } else {
                fprintf(stderr, "ld: unknown -z flag '%s'\n", argv[i]);
            }
        } else if (strcmp(argv[i], "--version-script") == 0) {
            if (i + 1 >= argc) {
                usage();
            }
            opts.version_script = argv[++i];
        } else if (strncmp(argv[i], "--version-script=", 17) == 0) {
            opts.version_script = argv[i] + 17;
        } else if (strcmp(argv[i], "--strip-debug") == 0) {
            opts.strip_debug = 1;
        } else if (strcmp(argv[i], "--strip-all") == 0 ||
                   strcmp(argv[i], "-s") == 0) {
            opts.strip_all = 1;
            opts.strip_debug = 1;
        } else if (strcmp(argv[i], "--start-group") == 0 ||
                   strcmp(argv[i], "-(") == 0) {
            in_group = 1;
        } else if (strcmp(argv[i], "--end-group") == 0 ||
                   strcmp(argv[i], "-)") == 0) {
            in_group = 0;
        } else if (strcmp(argv[i], "--warn-common") == 0) {
            /* accepted as no-op */
        } else if (strcmp(argv[i], "--no-warn-mismatch") == 0) {
            /* accepted as no-op */
        } else if (strcmp(argv[i], "--fatal-warnings") == 0) {
            opts.fatal_warnings = 1;
        } else if (strcmp(argv[i], "-EL") == 0) {
            /* accepted as no-op (always little-endian) */
        } else if (strncmp(argv[i], "--hash-style=", 13) == 0) {
            /* accepted as no-op (always sysv) */
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "ld: unknown option '%s'\n", argv[i]);
            usage();
        } else {
            /*
             * Input file: if --whole-archive or --start-group is active
             * and this is an AR archive, extract all members.
             * (Groups extract all members since we resolve globally.)
             */
            if ((whole_archive || in_group) && is_ar_file(argv[i])) {
                extract_archive_all(argv[i], &inputs,
                                    &num_inputs, &input_cap);
            } else {
                if (num_inputs >= input_cap) {
                    input_cap *= 2;
                    inputs = (const char **)realloc(inputs,
                        (unsigned long)input_cap * sizeof(const char *));
                }
                inputs[num_inputs++] = argv[i];
            }
        }
    }

    if (num_inputs == 0) {
        fprintf(stderr, "ld: no input files\n");
        usage();
    }

    /* read all input objects */
    objs = (struct elf_obj *)calloc((unsigned long)num_inputs,
                                   sizeof(struct elf_obj));
    if (!objs) {
        fprintf(stderr, "ld: out of memory\n");
        exit(1);
    }

    for (i = 0; i < num_inputs; i++) {
        elf_read(inputs[i], &objs[i]);
    }

    /* ---- relocatable mode (-r / --relocatable) ---- */
    if (relocatable_mode) {
        int ret;
        ret = link_relocatable(output, objs, num_inputs);
        for (i = 0; i < num_inputs; i++) {
            elf_obj_free(&objs[i]);
        }
        free(objs);
        free((void *)inputs);
        return ret;
    }

    /* process version script if specified */
    if (opts.version_script) {
        parse_version_script(opts.version_script);
    }

    /* PIE mode */
    if (opts.pie) {
        int ret;
        ret = link_pie(output, entry_name, objs, num_inputs, &opts);
        for (i = 0; i < num_inputs; i++) {
            elf_obj_free(&objs[i]);
        }
        free(objs);
        free((void *)inputs);
        for (i = 0; i < opts.num_defsyms; i++) {
            free(opts.defsym_names[i]);
        }
        free(opts.defsym_names);
        free(opts.defsym_values);
        return ret;
    }

    /* shared library mode */
    if (shared_mode) {
        int ret;
        if (soname == NULL) {
            soname = output;
        }
        ret = link_shared(output, soname, objs, num_inputs,
                          inputs, num_inputs);
        for (i = 0; i < num_inputs; i++) {
            elf_obj_free(&objs[i]);
        }
        free(objs);
        free((void *)inputs);
        return ret;
    }

    /* linker script mode: use script to drive layout */
    if (script_path) {
        struct ld_script *script;
        const char *script_entry_sym;

        script = script_read(script_path);

        /* script ENTRY overrides -e unless -e was explicitly given */
        script_entry_sym = script_entry(script);
        if (script_entry_sym) {
            entry_name = script_entry_sym;
        }

        /* resolve symbols - allow undefined since script defines them */
        resolve_symbols_shared(objs, num_inputs);

        /* gc-sections if requested */
        if (gc_sections_flag) {
            gc_sections(objs, num_inputs, entry_name);
        }

        /* script-driven layout (also defines script symbols) */
        script_layout(script, objs, num_inputs, &msecs, &num_msecs,
                      phdrs, &num_phdrs);

        /* compute final symbol addresses */
        compute_symbol_addrs(objs, num_inputs, msecs, num_msecs);

        /* apply relocations */
        apply_relocations(objs, num_inputs);

        /* copy patched data back into merged sections */
        for (i = 0; i < num_msecs; i++) {
            int j;
            for (j = 0; j < msecs[i].num_inputs; j++) {
                struct input_piece *ip;
                struct section *sec;
                ip = &msecs[i].inputs[j];
                sec = &objs[ip->obj_idx].sections[ip->sec_idx];
                if (sec->shdr.sh_type != SHT_NOBITS && sec->data) {
                    memcpy(msecs[i].data + ip->offset_in_merged,
                           sec->data,
                           (unsigned long)sec->shdr.sh_size);
                }
            }
        }

        /* find entry point (use safe variant: script may define it) */
        if (!find_entry_symbol_safe(entry_name, &entry_addr)) {
            fprintf(stderr, "ld: warning: entry symbol '%s' not found,"
                    " defaulting to 0\n", entry_name);
            entry_addr = 0;
        }

        /* write map file if requested */
        if (map_path) {
            write_map_file(map_path, msecs, num_msecs,
                           objs, num_inputs, entry_addr);
        }

        /* build output section array (alloc + symtab + strtab) */
        num_out_secs = num_msecs + (opts.strip_all ? 0 : 2);
        out_secs = (struct out_section *)calloc(
            (unsigned long)num_out_secs, sizeof(struct out_section));
        for (i = 0; i < num_msecs; i++) {
            memcpy(&out_secs[i].shdr, &msecs[i].shdr,
                   sizeof(Elf64_Shdr));
            out_secs[i].data = msecs[i].data;
            out_secs[i].size = msecs[i].size;
        }

        /* Build .symtab and .strtab for the output */
        if (!opts.strip_all) {
            Elf64_Sym *exec_syms;
            int exec_nsyms;
            int exec_sym_cap;
            u8 *exec_strtab;
            u32 exec_strtab_size;
            u32 exec_strtab_cap;
            int exec_first_global;
            int symtab_out_idx;
            int strtab_out_idx;
            int oi2;
            int si2;
            u64 sym_file_off;
            const char **gs_names;
            u64 *gs_addrs;
            int *gs_defined;
            int *gs_weak;
            int *gs_obj_idxs;
            int *gs_sym_idxs;
            int gs_count;

            exec_sym_cap = 256;
            exec_syms = (Elf64_Sym *)calloc(
                (unsigned long)exec_sym_cap, sizeof(Elf64_Sym));
            exec_nsyms = 1; /* entry 0 is null */
            exec_strtab_cap = 4096;
            exec_strtab = (u8 *)malloc(exec_strtab_cap);
            exec_strtab[0] = '\0';
            exec_strtab_size = 1;

            /* Pass 1: local symbols */
            for (oi2 = 0; oi2 < num_inputs; oi2++) {
                for (si2 = 0; si2 < objs[oi2].num_symbols; si2++) {
                    Elf64_Sym *isym = &objs[oi2].symbols[si2].sym;
                    int bind2 = ELF64_ST_BIND(isym->st_info);
                    int type2 = ELF64_ST_TYPE(isym->st_info);
                    u32 noff;
                    unsigned long nlen;
                    Elf64_Sym *os2;

                    if (bind2 != STB_LOCAL) continue;
                    if (type2 == STT_SECTION) continue;
                    if (isym->st_name == 0) continue;
                    if (objs[oi2].symbols[si2].name == NULL) continue;
                    if (objs[oi2].symbols[si2].name[0] == '\0') continue;
                    if (isym->st_shndx == SHN_UNDEF) continue;

                    if (exec_nsyms >= exec_sym_cap) {
                        exec_sym_cap *= 2;
                        exec_syms = (Elf64_Sym *)realloc(exec_syms,
                            (unsigned long)exec_sym_cap *
                            sizeof(Elf64_Sym));
                    }
                    nlen = strlen(objs[oi2].symbols[si2].name) + 1;
                    if (exec_strtab_size + nlen > exec_strtab_cap) {
                        while (exec_strtab_size + nlen > exec_strtab_cap)
                            exec_strtab_cap *= 2;
                        exec_strtab = (u8 *)realloc(exec_strtab,
                            exec_strtab_cap);
                    }
                    noff = exec_strtab_size;
                    memcpy(exec_strtab + exec_strtab_size,
                           objs[oi2].symbols[si2].name, nlen);
                    exec_strtab_size += (u32)nlen;

                    os2 = &exec_syms[exec_nsyms];
                    os2->st_name = noff;
                    os2->st_info = isym->st_info;
                    os2->st_other = isym->st_other;
                    os2->st_shndx = SHN_ABS;
                    os2->st_value = objs[oi2].symbols[si2].resolved_addr;
                    os2->st_size = isym->st_size;
                    exec_nsyms++;
                }
            }

            exec_first_global = exec_nsyms;

            /* Pass 2: global symbols */
            reloc_get_global_syms(&gs_names, &gs_addrs,
                                  &gs_defined, &gs_weak,
                                  &gs_obj_idxs, &gs_sym_idxs,
                                  &gs_count);
            for (si2 = 0; si2 < gs_count; si2++) {
                u32 noff;
                unsigned long nlen;
                Elf64_Sym *os2;
                u8 st_info;

                if (gs_names[si2] == NULL) continue;
                if (gs_names[si2][0] == '\0') continue;

                if (exec_nsyms >= exec_sym_cap) {
                    exec_sym_cap *= 2;
                    exec_syms = (Elf64_Sym *)realloc(exec_syms,
                        (unsigned long)exec_sym_cap *
                        sizeof(Elf64_Sym));
                }
                nlen = strlen(gs_names[si2]) + 1;
                if (exec_strtab_size + nlen > exec_strtab_cap) {
                    while (exec_strtab_size + nlen > exec_strtab_cap)
                        exec_strtab_cap *= 2;
                    exec_strtab = (u8 *)realloc(exec_strtab,
                        exec_strtab_cap);
                }
                noff = exec_strtab_size;
                memcpy(exec_strtab + exec_strtab_size,
                       gs_names[si2], nlen);
                exec_strtab_size += (u32)nlen;

                os2 = &exec_syms[exec_nsyms];
                os2->st_name = noff;
                st_info = gs_weak[si2]
                    ? ELF64_ST_INFO(STB_WEAK, STT_NOTYPE)
                    : ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
                if (gs_defined[si2] && gs_obj_idxs[si2] >= 0 &&
                    gs_sym_idxs[si2] >= 0) {
                    int oidx = gs_obj_idxs[si2];
                    int sidx = gs_sym_idxs[si2];
                    if (oidx < num_inputs &&
                        sidx < objs[oidx].num_symbols) {
                        int stype = ELF64_ST_TYPE(
                            objs[oidx].symbols[sidx].sym.st_info);
                        int sbind = gs_weak[si2]
                            ? STB_WEAK : STB_GLOBAL;
                        st_info = ELF64_ST_INFO(sbind, stype);
                        os2->st_size =
                            objs[oidx].symbols[sidx].sym.st_size;
                    }
                }
                os2->st_info = st_info;
                os2->st_other = STV_DEFAULT;
                if (gs_defined[si2]) {
                    os2->st_shndx = SHN_ABS;
                    os2->st_value = gs_addrs[si2];
                } else {
                    os2->st_shndx = SHN_UNDEF;
                    os2->st_value = 0;
                }
                exec_nsyms++;
            }
            free((void *)gs_names);
            free(gs_addrs);
            free(gs_defined);
            free(gs_weak);
            free(gs_obj_idxs);
            free(gs_sym_idxs);

            /* compute file offset after all alloc data */
            sym_file_off = 0;
            for (i = 0; i < num_msecs; i++) {
                u64 end;
                if (msecs[i].shdr.sh_type == SHT_NOBITS) continue;
                end = msecs[i].shdr.sh_offset + msecs[i].size;
                if (end > sym_file_off) sym_file_off = end;
            }

            symtab_out_idx = num_msecs;
            strtab_out_idx = num_msecs + 1;

            sym_file_off = (sym_file_off + 7) & ~(u64)7;
            memset(&out_secs[symtab_out_idx].shdr, 0,
                   sizeof(Elf64_Shdr));
            out_secs[symtab_out_idx].shdr.sh_type = SHT_SYMTAB;
            out_secs[symtab_out_idx].shdr.sh_flags = 0;
            out_secs[symtab_out_idx].shdr.sh_offset = sym_file_off;
            out_secs[symtab_out_idx].shdr.sh_size =
                (u64)exec_nsyms * sizeof(Elf64_Sym);
            out_secs[symtab_out_idx].shdr.sh_link =
                (u32)(strtab_out_idx + 1);
            out_secs[symtab_out_idx].shdr.sh_info =
                (u32)exec_first_global;
            out_secs[symtab_out_idx].shdr.sh_addralign = 8;
            out_secs[symtab_out_idx].shdr.sh_entsize =
                sizeof(Elf64_Sym);
            out_secs[symtab_out_idx].data = (u8 *)exec_syms;
            out_secs[symtab_out_idx].size =
                (unsigned long)exec_nsyms * sizeof(Elf64_Sym);
            sym_file_off += out_secs[symtab_out_idx].size;

            memset(&out_secs[strtab_out_idx].shdr, 0,
                   sizeof(Elf64_Shdr));
            out_secs[strtab_out_idx].shdr.sh_type = SHT_STRTAB;
            out_secs[strtab_out_idx].shdr.sh_flags = 0;
            out_secs[strtab_out_idx].shdr.sh_offset = sym_file_off;
            out_secs[strtab_out_idx].shdr.sh_size = exec_strtab_size;
            out_secs[strtab_out_idx].shdr.sh_addralign = 1;
            out_secs[strtab_out_idx].data = exec_strtab;
            out_secs[strtab_out_idx].size = exec_strtab_size;
        }

        /* build shstrtab */
        name_offsets = (u32 *)calloc(
            (unsigned long)num_out_secs, sizeof(u32));
        {
            u32 cap = 512;
            u32 pos = 0;

            shstrtab_data = (u8 *)malloc(cap);
            shstrtab_data[pos++] = '\0';
            {
                const char *s = ".shstrtab";
                u32 len = (u32)(strlen(s) + 1);
                memcpy(shstrtab_data + pos, s, len);
                pos += len;
            }
            for (i = 0; i < num_msecs; i++) {
                u32 len = (u32)(strlen(msecs[i].name) + 1);
                if (pos + len > cap) {
                    while (pos + len > cap) cap *= 2;
                    shstrtab_data = (u8 *)realloc(shstrtab_data, cap);
                }
                name_offsets[i] = pos;
                memcpy(shstrtab_data + pos, msecs[i].name, len);
                pos += len;
            }
            /* .symtab and .strtab names (omitted with --strip-all) */
            if (!opts.strip_all) {
                const char *sym_name = ".symtab";
                const char *str_name = ".strtab";
                u32 len;

                len = (u32)(strlen(sym_name) + 1);
                if (pos + len > cap) {
                    while (pos + len > cap) cap *= 2;
                    shstrtab_data = (u8 *)realloc(
                        shstrtab_data, cap);
                }
                name_offsets[num_out_secs - 2] = pos;
                memcpy(shstrtab_data + pos, sym_name, len);
                pos += len;

                len = (u32)(strlen(str_name) + 1);
                if (pos + len > cap) {
                    while (pos + len > cap) cap *= 2;
                    shstrtab_data = (u8 *)realloc(
                        shstrtab_data, cap);
                }
                name_offsets[num_out_secs - 1] = pos;
                memcpy(shstrtab_data + pos, str_name, len);
                pos += len;
            }
            shstrtab_size = pos;
        }
        for (i = 0; i < num_out_secs; i++) {
            out_secs[i].shdr.sh_name = name_offsets[i];
        }

        /* compute shstrtab offset */
        {
            u64 max_end = 0;
            for (i = 0; i < num_out_secs; i++) {
                u64 end;
                if (out_secs[i].shdr.sh_type == SHT_NOBITS) continue;
                end = out_secs[i].shdr.sh_offset + out_secs[i].size;
                if (end > max_end) max_end = end;
            }
            shstrtab_offset = max_end;
        }
        sh_offset = shstrtab_offset + (u64)shstrtab_size;
        sh_offset = (sh_offset + 7) & ~(u64)7;

        /* build ELF header */
        memset(&ehdr, 0, sizeof(ehdr));
        ehdr.e_ident[0] = ELFMAG0;
        ehdr.e_ident[1] = ELFMAG1;
        ehdr.e_ident[2] = ELFMAG2;
        ehdr.e_ident[3] = ELFMAG3;
        ehdr.e_ident[4] = ELFCLASS64;
        ehdr.e_ident[5] = ELFDATA2LSB;
        ehdr.e_ident[6] = EV_CURRENT;
        ehdr.e_ident[7] = ELFOSABI_NONE;
        ehdr.e_type = ET_EXEC;
        ehdr.e_machine = EM_AARCH64;
        ehdr.e_version = EV_CURRENT;
        ehdr.e_entry = entry_addr;
        ehdr.e_phoff = sizeof(Elf64_Ehdr);
        ehdr.e_shoff = sh_offset;
        ehdr.e_flags = 0;
        ehdr.e_ehsize = sizeof(Elf64_Ehdr);
        ehdr.e_phentsize = sizeof(Elf64_Phdr);
        ehdr.e_phnum = (u16)num_phdrs;
        ehdr.e_shentsize = sizeof(Elf64_Shdr);
        ehdr.e_shnum = (u16)(num_out_secs + 2);
        ehdr.e_shstrndx = (u16)(num_out_secs + 1);

        elf_write_exec(output, &ehdr, phdrs, num_phdrs,
                       out_secs, num_out_secs,
                       shstrtab_offset, shstrtab_data, shstrtab_size);

        /* free symtab/strtab data */
        if (!opts.strip_all) {
            free(out_secs[num_msecs].data);
            free(out_secs[num_msecs + 1].data);
        }
        free(out_secs);
        free(name_offsets);
        free(shstrtab_data);
        reloc_cleanup();
        layout_free(msecs, num_msecs);
        script_free(script);
        for (i = 0; i < num_inputs; i++) {
            elf_obj_free(&objs[i]);
        }
        free(objs);
        free((void *)inputs);
        return 0;
    }

    /* attempt LTO if any inputs have .free_ir sections */
    try_lto(inputs, num_inputs, objs);

    /* resolve symbols across all objects */
    resolve_symbols(objs, num_inputs);

    /* add --defsym symbols */
    for (i = 0; i < opts.num_defsyms; i++) {
        add_defsym(opts.defsym_names[i], opts.defsym_values[i]);
    }

    /* check --no-undefined */
    if (opts.no_undefined) {
        check_undefined_symbols();
    }

    /* gc-sections: mark and discard unreachable sections */
    if (gc_sections_flag) {
        gc_sections(objs, num_inputs, entry_name);
    }

    /* merge sections and assign addresses */
    layout_sections(objs, num_inputs, &msecs, &num_msecs,
                    phdrs, &num_phdrs);

    /* compute final symbol addresses */
    compute_symbol_addrs(objs, num_inputs, msecs, num_msecs);

    /* apply relocations */
    apply_relocations(objs, num_inputs);

    /*
     * After relocations, copy patched data back into merged sections.
     * Each input piece's data in the object now has relocations applied.
     */
    for (i = 0; i < num_msecs; i++) {
        int j;
        for (j = 0; j < msecs[i].num_inputs; j++) {
            struct input_piece *ip;
            struct section *sec;

            /* access via merged_section's inputs */
            ip = &msecs[i].inputs[j];
            sec = &objs[ip->obj_idx].sections[ip->sec_idx];

            if (sec->shdr.sh_type != SHT_NOBITS && sec->data) {
                memcpy(msecs[i].data + ip->offset_in_merged,
                       sec->data, (unsigned long)sec->shdr.sh_size);
            }
        }
    }

    /* find entry point */
    entry_addr = find_entry_symbol(entry_name);

    /* write map file if requested */
    if (map_path) {
        write_map_file(map_path, msecs, num_msecs,
                       objs, num_inputs, entry_addr);
    }

    /*
     * Collect non-alloc debug sections (.debug_*) from input objects.
     * These are merged by name and placed after all LOAD segment data
     * with no virtual address (addr=0).
     */
    {
        int num_dbg;
        int dbg_cap;
        struct merged_section *dbg_msecs;

        /* emit-relocs: collect relocation sections to preserve */
        int num_emit_rela;
        int emit_rela_cap;
        struct out_section *emit_rela_secs;
        char **emit_rela_names;

        /* build-id data */
        u8 *buildid_data;
        u32 buildid_size;

        buildid_data = NULL;
        buildid_size = 0;

        dbg_cap = 8;
        num_dbg = 0;
        dbg_msecs = (struct merged_section *)calloc(
            (unsigned long)dbg_cap, sizeof(struct merged_section));

        for (i = 0; i < num_inputs && !opts.strip_debug; i++) {
            int j;
            for (j = 0; j < objs[i].num_sections; j++) {
                struct section *sec = &objs[i].sections[j];
                int di;

                if (sec->name == NULL) continue;
                if (sec->shdr.sh_flags & SHF_ALLOC) continue;
                if (sec->shdr.sh_type != SHT_PROGBITS) continue;
                if (strncmp(sec->name, ".debug_", 7) != 0) continue;
                if (sec->shdr.sh_size == 0) continue;

                /* find or create merged debug section */
                di = -1;
                {
                    int m;
                    for (m = 0; m < num_dbg; m++) {
                        if (strcmp(dbg_msecs[m].name, sec->name) == 0) {
                            di = m;
                            break;
                        }
                    }
                }
                if (di < 0) {
                    if (num_dbg >= dbg_cap) {
                        dbg_cap *= 2;
                        dbg_msecs = (struct merged_section *)realloc(
                            dbg_msecs,
                            (unsigned long)dbg_cap *
                            sizeof(struct merged_section));
                    }
                    di = num_dbg++;
                    memset(&dbg_msecs[di], 0,
                           sizeof(struct merged_section));
                    dbg_msecs[di].name = sec->name;
                    dbg_msecs[di].shdr.sh_type = SHT_PROGBITS;
                    dbg_msecs[di].shdr.sh_flags = 0;
                    dbg_msecs[di].shdr.sh_addralign = 1;
                }

                /* append data */
                {
                    unsigned long new_size;
                    new_size = dbg_msecs[di].size +
                               (unsigned long)sec->shdr.sh_size;
                    if (new_size > dbg_msecs[di].capacity) {
                        unsigned long nc;
                        nc = dbg_msecs[di].capacity == 0
                            ? 4096 : dbg_msecs[di].capacity;
                        while (nc < new_size) nc *= 2;
                        dbg_msecs[di].data = (u8 *)realloc(
                            dbg_msecs[di].data, nc);
                        if (!dbg_msecs[di].data) {
                            fprintf(stderr, "ld: out of memory\n");
                            exit(1);
                        }
                        dbg_msecs[di].capacity = nc;
                    }
                    if (sec->data) {
                        memcpy(dbg_msecs[di].data + dbg_msecs[di].size,
                               sec->data,
                               (unsigned long)sec->shdr.sh_size);
                    }
                    dbg_msecs[di].size = new_size;
                }
            }
        }

        /* ---- emit-relocs: collect relocation data ---- */
        emit_rela_cap = 8;
        num_emit_rela = 0;
        emit_rela_secs = NULL;
        emit_rela_names = NULL;

        if (emit_relocs_flag) {
            emit_rela_secs = (struct out_section *)calloc(
                (unsigned long)emit_rela_cap, sizeof(struct out_section));
            emit_rela_names = (char **)calloc(
                (unsigned long)emit_rela_cap, sizeof(char *));

            /*
             * For each merged section, gather all relocations that
             * target it and build a .rela section for the output.
             * The relocations have already been applied, but we
             * preserve the original rela entries with updated offsets
             * for runtime use (KASLR).
             */
            for (i = 0; i < num_msecs; i++) {
                Elf64_Rela *rela_buf;
                int rela_count;
                int rela_buf_cap;
                int oi;
                int ri;

                rela_buf_cap = 64;
                rela_buf = (Elf64_Rela *)xcalloc_ld(
                    (unsigned long)rela_buf_cap, sizeof(Elf64_Rela));
                rela_count = 0;

                for (oi = 0; oi < num_inputs; oi++) {
                    for (ri = 0; ri < objs[oi].num_relas; ri++) {
                        struct elf_rela *r = &objs[oi].relas[ri];
                        int sec_idx = r->target_section;
                        int k2;
                        int found;

                        if (sec_idx <= 0 || sec_idx >= objs[oi].num_sections) {
                            continue;
                        }

                        /* check if this input section contributes to
                         * merged section i */
                        found = 0;
                        for (k2 = 0; k2 < msecs[i].num_inputs; k2++) {
                            struct input_piece *ip = &msecs[i].inputs[k2];
                            if (ip->obj_idx == oi &&
                                ip->sec_idx == sec_idx) {
                                /* adjust r_offset */
                                Elf64_Rela adj;
                                adj.r_offset = r->rela.r_offset +
                                               ip->offset_in_merged;
                                adj.r_info = r->rela.r_info;
                                adj.r_addend = r->rela.r_addend;

                                if (rela_count >= rela_buf_cap) {
                                    rela_buf_cap *= 2;
                                    rela_buf = (Elf64_Rela *)realloc(
                                        rela_buf,
                                        (unsigned long)rela_buf_cap *
                                        sizeof(Elf64_Rela));
                                }
                                rela_buf[rela_count++] = adj;
                                found = 1;
                                break;
                            }
                        }
                        (void)found;
                    }
                }

                if (rela_count > 0) {
                    char *rname;
                    unsigned long rname_len;

                    if (num_emit_rela >= emit_rela_cap) {
                        emit_rela_cap *= 2;
                        emit_rela_secs = (struct out_section *)realloc(
                            emit_rela_secs,
                            (unsigned long)emit_rela_cap *
                            sizeof(struct out_section));
                        emit_rela_names = (char **)realloc(
                            emit_rela_names,
                            (unsigned long)emit_rela_cap *
                            sizeof(char *));
                    }

                    rname_len = strlen(msecs[i].name) + 6;
                    rname = (char *)xmalloc(rname_len);
                    sprintf(rname, ".rela%s", msecs[i].name);

                    memset(&emit_rela_secs[num_emit_rela].shdr, 0,
                           sizeof(Elf64_Shdr));
                    emit_rela_secs[num_emit_rela].shdr.sh_type = SHT_RELA;
                    emit_rela_secs[num_emit_rela].shdr.sh_flags = 0;
                    emit_rela_secs[num_emit_rela].shdr.sh_entsize =
                        sizeof(Elf64_Rela);
                    emit_rela_secs[num_emit_rela].shdr.sh_addralign = 8;
                    emit_rela_secs[num_emit_rela].data = (u8 *)rela_buf;
                    emit_rela_secs[num_emit_rela].size =
                        (unsigned long)rela_count * sizeof(Elf64_Rela);
                    emit_rela_secs[num_emit_rela].shdr.sh_size =
                        emit_rela_secs[num_emit_rela].size;
                    emit_rela_names[num_emit_rela] = rname;
                    num_emit_rela++;
                } else {
                    free(rela_buf);
                }
            }
        }

        /* build combined out_section array:
         * alloc + debug + emit_rela + build-id + symtab + strtab */
        num_out_secs = num_msecs + num_dbg + num_emit_rela;
        if (!opts.strip_all) {
            num_out_secs += 2; /* .symtab + .strtab */
        }
        if (build_id_flag) {
            num_out_secs += 1;
        }

        out_secs = (struct out_section *)calloc(
            (unsigned long)num_out_secs, sizeof(struct out_section));
        for (i = 0; i < num_msecs; i++) {
            memcpy(&out_secs[i].shdr, &msecs[i].shdr,
                   sizeof(Elf64_Shdr));
            out_secs[i].data = msecs[i].data;
            out_secs[i].size = msecs[i].size;
        }

        /* assign file offsets for debug sections after alloc data */
        {
            u64 dbg_offset = 0;
            int out_idx;

            for (i = 0; i < num_msecs; i++) {
                u64 end;
                if (msecs[i].shdr.sh_type == SHT_NOBITS) continue;
                end = msecs[i].shdr.sh_offset + msecs[i].size;
                if (end > dbg_offset) dbg_offset = end;
            }

            for (i = 0; i < num_dbg; i++) {
                int oi2 = num_msecs + i;
                memset(&out_secs[oi2].shdr, 0, sizeof(Elf64_Shdr));
                out_secs[oi2].shdr.sh_type = SHT_PROGBITS;
                out_secs[oi2].shdr.sh_flags = 0;
                out_secs[oi2].shdr.sh_addr = 0;
                out_secs[oi2].shdr.sh_offset = dbg_offset;
                out_secs[oi2].shdr.sh_size = dbg_msecs[i].size;
                out_secs[oi2].shdr.sh_addralign = 1;
                out_secs[oi2].data = dbg_msecs[i].data;
                out_secs[oi2].size = dbg_msecs[i].size;
                dbg_offset += dbg_msecs[i].size;
            }

            /* emit-relocs sections */
            out_idx = num_msecs + num_dbg;
            for (i = 0; i < num_emit_rela; i++) {
                dbg_offset = (dbg_offset + 7) & ~(u64)7;
                out_secs[out_idx].shdr = emit_rela_secs[i].shdr;
                out_secs[out_idx].shdr.sh_offset = dbg_offset;
                out_secs[out_idx].data = emit_rela_secs[i].data;
                out_secs[out_idx].size = emit_rela_secs[i].size;
                dbg_offset += emit_rela_secs[i].size;
                out_idx++;
            }

            /* build-id section (generated after all other data) */
            if (build_id_flag) {
                /* build a preliminary build-id from existing sections */
                buildid_data = build_note_build_id(
                    out_secs, out_idx, &buildid_size);

                dbg_offset = (dbg_offset + 3) & ~(u64)3;
                memset(&out_secs[out_idx].shdr, 0, sizeof(Elf64_Shdr));
                out_secs[out_idx].shdr.sh_type = SHT_NOTE;
                out_secs[out_idx].shdr.sh_flags = SHF_ALLOC;
                out_secs[out_idx].shdr.sh_offset = dbg_offset;
                out_secs[out_idx].shdr.sh_size = buildid_size;
                out_secs[out_idx].shdr.sh_addralign = 4;
                out_secs[out_idx].data = buildid_data;
                out_secs[out_idx].size = buildid_size;
                out_idx++;
            }

            /*
             * Build .symtab and .strtab for the output executable.
             * Collect all defined symbols from input objects with
             * their resolved addresses, plus global symbols.
             * Skipped when --strip-all / -s is active.
             */
            if (!opts.strip_all) {
                Elf64_Sym *exec_syms;
                int exec_nsyms;
                int exec_sym_cap;
                u8 *exec_strtab;
                u32 exec_strtab_size;
                u32 exec_strtab_cap;
                int exec_first_global;
                int symtab_out_idx;
                int strtab_out_idx;
                int oi2;
                int si2;
                const char **gs_names;
                u64 *gs_addrs;
                int *gs_defined;
                int *gs_weak;
                int *gs_obj_idxs;
                int *gs_sym_idxs;
                int gs_count;

                exec_sym_cap = 256;
                exec_syms = (Elf64_Sym *)calloc(
                    (unsigned long)exec_sym_cap, sizeof(Elf64_Sym));
                exec_nsyms = 1; /* entry 0 is null */
                exec_strtab_cap = 4096;
                exec_strtab = (u8 *)malloc(exec_strtab_cap);
                exec_strtab[0] = '\0';
                exec_strtab_size = 1;

                /* Pass 1: local symbols from all input objects */
                for (oi2 = 0; oi2 < num_inputs; oi2++) {
                    for (si2 = 0; si2 < objs[oi2].num_symbols; si2++) {
                        Elf64_Sym *isym = &objs[oi2].symbols[si2].sym;
                        int bind2 = ELF64_ST_BIND(isym->st_info);
                        int type2 = ELF64_ST_TYPE(isym->st_info);
                        u32 noff;
                        unsigned long nlen;
                        Elf64_Sym *os2;

                        if (bind2 != STB_LOCAL) continue;
                        if (type2 == STT_SECTION) continue;
                        if (isym->st_name == 0) continue;
                        if (objs[oi2].symbols[si2].name == NULL) continue;
                        if (objs[oi2].symbols[si2].name[0] == '\0') continue;
                        if (isym->st_shndx == SHN_UNDEF) continue;

                        /* grow arrays */
                        if (exec_nsyms >= exec_sym_cap) {
                            exec_sym_cap *= 2;
                            exec_syms = (Elf64_Sym *)realloc(exec_syms,
                                (unsigned long)exec_sym_cap *
                                sizeof(Elf64_Sym));
                        }
                        nlen = strlen(objs[oi2].symbols[si2].name) + 1;
                        if (exec_strtab_size + nlen > exec_strtab_cap) {
                            while (exec_strtab_size + nlen > exec_strtab_cap)
                                exec_strtab_cap *= 2;
                            exec_strtab = (u8 *)realloc(exec_strtab,
                                exec_strtab_cap);
                        }
                        noff = exec_strtab_size;
                        memcpy(exec_strtab + exec_strtab_size,
                               objs[oi2].symbols[si2].name, nlen);
                        exec_strtab_size += (u32)nlen;

                        os2 = &exec_syms[exec_nsyms];
                        os2->st_name = noff;
                        os2->st_info = isym->st_info;
                        os2->st_other = isym->st_other;
                        os2->st_shndx = SHN_ABS;
                        os2->st_value = objs[oi2].symbols[si2].resolved_addr;
                        os2->st_size = isym->st_size;
                        exec_nsyms++;
                    }
                }

                exec_first_global = exec_nsyms;

                /* Pass 2: global symbols via accessor */
                reloc_get_global_syms(&gs_names, &gs_addrs,
                                      &gs_defined, &gs_weak,
                                      &gs_obj_idxs, &gs_sym_idxs,
                                      &gs_count);
                for (si2 = 0; si2 < gs_count; si2++) {
                    u32 noff;
                    unsigned long nlen;
                    Elf64_Sym *os2;
                    u8 st_info;

                    if (gs_names[si2] == NULL) continue;
                    if (gs_names[si2][0] == '\0') continue;

                    if (exec_nsyms >= exec_sym_cap) {
                        exec_sym_cap *= 2;
                        exec_syms = (Elf64_Sym *)realloc(exec_syms,
                            (unsigned long)exec_sym_cap *
                            sizeof(Elf64_Sym));
                    }
                    nlen = strlen(gs_names[si2]) + 1;
                    if (exec_strtab_size + nlen > exec_strtab_cap) {
                        while (exec_strtab_size + nlen > exec_strtab_cap)
                            exec_strtab_cap *= 2;
                        exec_strtab = (u8 *)realloc(exec_strtab,
                            exec_strtab_cap);
                    }
                    noff = exec_strtab_size;
                    memcpy(exec_strtab + exec_strtab_size,
                           gs_names[si2], nlen);
                    exec_strtab_size += (u32)nlen;

                    os2 = &exec_syms[exec_nsyms];
                    os2->st_name = noff;
                    st_info = gs_weak[si2]
                        ? ELF64_ST_INFO(STB_WEAK, STT_NOTYPE)
                        : ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
                    /* Try to get type from defining object */
                    if (gs_defined[si2] && gs_obj_idxs[si2] >= 0 &&
                        gs_sym_idxs[si2] >= 0) {
                        int oidx = gs_obj_idxs[si2];
                        int sidx = gs_sym_idxs[si2];
                        if (oidx < num_inputs &&
                            sidx < objs[oidx].num_symbols) {
                            int stype = ELF64_ST_TYPE(
                                objs[oidx].symbols[sidx].sym.st_info);
                            int sbind = gs_weak[si2]
                                ? STB_WEAK : STB_GLOBAL;
                            st_info = ELF64_ST_INFO(sbind, stype);
                            os2->st_size =
                                objs[oidx].symbols[sidx].sym.st_size;
                        }
                    }
                    os2->st_info = st_info;
                    os2->st_other = STV_DEFAULT;
                    if (gs_defined[si2]) {
                        os2->st_shndx = SHN_ABS;
                        os2->st_value = gs_addrs[si2];
                    } else {
                        os2->st_shndx = SHN_UNDEF;
                        os2->st_value = 0;
                    }
                    exec_nsyms++;
                }
                free((void *)gs_names);
                free(gs_addrs);
                free(gs_defined);
                free(gs_weak);
                free(gs_obj_idxs);
                free(gs_sym_idxs);

                /* Place .symtab and .strtab in output */
                symtab_out_idx = out_idx;
                strtab_out_idx = out_idx + 1;

                /* .symtab */
                dbg_offset = (dbg_offset + 7) & ~(u64)7;
                memset(&out_secs[symtab_out_idx].shdr, 0,
                       sizeof(Elf64_Shdr));
                out_secs[symtab_out_idx].shdr.sh_type = SHT_SYMTAB;
                out_secs[symtab_out_idx].shdr.sh_flags = 0;
                out_secs[symtab_out_idx].shdr.sh_offset = dbg_offset;
                out_secs[symtab_out_idx].shdr.sh_size =
                    (u64)exec_nsyms * sizeof(Elf64_Sym);
                /* sh_link points to .strtab section header index
                 * (1-based, accounting for null section header) */
                out_secs[symtab_out_idx].shdr.sh_link =
                    (u32)(strtab_out_idx + 1);
                out_secs[symtab_out_idx].shdr.sh_info =
                    (u32)exec_first_global;
                out_secs[symtab_out_idx].shdr.sh_addralign = 8;
                out_secs[symtab_out_idx].shdr.sh_entsize =
                    sizeof(Elf64_Sym);
                out_secs[symtab_out_idx].data = (u8 *)exec_syms;
                out_secs[symtab_out_idx].size =
                    (unsigned long)exec_nsyms * sizeof(Elf64_Sym);
                dbg_offset += out_secs[symtab_out_idx].size;
                out_idx++;

                /* .strtab */
                memset(&out_secs[strtab_out_idx].shdr, 0,
                       sizeof(Elf64_Shdr));
                out_secs[strtab_out_idx].shdr.sh_type = SHT_STRTAB;
                out_secs[strtab_out_idx].shdr.sh_flags = 0;
                out_secs[strtab_out_idx].shdr.sh_offset = dbg_offset;
                out_secs[strtab_out_idx].shdr.sh_size = exec_strtab_size;
                out_secs[strtab_out_idx].shdr.sh_addralign = 1;
                out_secs[strtab_out_idx].data = exec_strtab;
                out_secs[strtab_out_idx].size = exec_strtab_size;
                dbg_offset += exec_strtab_size;
                out_idx++;
            }
        }

        /* build shstrtab including debug, emit-rela, and build-id names */
        name_offsets = (u32 *)calloc(
            (unsigned long)num_out_secs, sizeof(u32));
        {
            u32 cap = 512;
            u32 pos = 0;
            shstrtab_data = (u8 *)malloc(cap);

            /* null byte */
            shstrtab_data[pos++] = '\0';

            /* ".shstrtab" */
            {
                const char *s = ".shstrtab";
                u32 len = (u32)(strlen(s) + 1);
                memcpy(shstrtab_data + pos, s, len);
                pos += len;
            }

            /* alloc section names */
            for (i = 0; i < num_msecs; i++) {
                u32 len = (u32)(strlen(msecs[i].name) + 1);
                if (pos + len > cap) {
                    while (pos + len > cap) cap *= 2;
                    shstrtab_data = (u8 *)realloc(
                        shstrtab_data, cap);
                }
                name_offsets[i] = pos;
                memcpy(shstrtab_data + pos, msecs[i].name, len);
                pos += len;
            }

            /* debug section names */
            for (i = 0; i < num_dbg; i++) {
                u32 len = (u32)(strlen(dbg_msecs[i].name) + 1);
                if (pos + len > cap) {
                    while (pos + len > cap) cap *= 2;
                    shstrtab_data = (u8 *)realloc(
                        shstrtab_data, cap);
                }
                name_offsets[num_msecs + i] = pos;
                memcpy(shstrtab_data + pos,
                       dbg_msecs[i].name, len);
                pos += len;
            }

            /* emit-relocs section names */
            for (i = 0; i < num_emit_rela; i++) {
                u32 len = (u32)(strlen(emit_rela_names[i]) + 1);
                if (pos + len > cap) {
                    while (pos + len > cap) cap *= 2;
                    shstrtab_data = (u8 *)realloc(
                        shstrtab_data, cap);
                }
                name_offsets[num_msecs + num_dbg + i] = pos;
                memcpy(shstrtab_data + pos,
                       emit_rela_names[i], len);
                pos += len;
            }

            /* build-id name */
            if (build_id_flag) {
                const char *bid_name = ".note.gnu.build-id";
                u32 len = (u32)(strlen(bid_name) + 1);
                if (pos + len > cap) {
                    while (pos + len > cap) cap *= 2;
                    shstrtab_data = (u8 *)realloc(
                        shstrtab_data, cap);
                }
                name_offsets[num_msecs + num_dbg + num_emit_rela] = pos;
                memcpy(shstrtab_data + pos, bid_name, len);
                pos += len;
            }

            /* .symtab and .strtab names (omitted with --strip-all) */
            if (!opts.strip_all) {
                const char *sym_name = ".symtab";
                const char *str_name = ".strtab";
                u32 len;

                len = (u32)(strlen(sym_name) + 1);
                if (pos + len > cap) {
                    while (pos + len > cap) cap *= 2;
                    shstrtab_data = (u8 *)realloc(
                        shstrtab_data, cap);
                }
                name_offsets[num_out_secs - 2] = pos;
                memcpy(shstrtab_data + pos, sym_name, len);
                pos += len;

                len = (u32)(strlen(str_name) + 1);
                if (pos + len > cap) {
                    while (pos + len > cap) cap *= 2;
                    shstrtab_data = (u8 *)realloc(
                        shstrtab_data, cap);
                }
                name_offsets[num_out_secs - 1] = pos;
                memcpy(shstrtab_data + pos, str_name, len);
                pos += len;
            }

            shstrtab_size = pos;
        }

        /* assign sh_name to all output sections */
        for (i = 0; i < num_out_secs; i++) {
            out_secs[i].shdr.sh_name = name_offsets[i];
        }

        /* free debug merged section array (data now owned by out_secs) */
        for (i = 0; i < num_dbg; i++) {
            free(dbg_msecs[i].inputs);
        }
        free(dbg_msecs);

        /* free emit-relocs names (data owned by out_secs) */
        for (i = 0; i < num_emit_rela; i++) {
            free(emit_rela_names[i]);
        }
        free(emit_rela_names);
        free(emit_rela_secs);
    }

    /* add PT_GNU_STACK if -z noexecstack */
    if (opts.z_noexecstack && num_phdrs < 4) {
        memset(&phdrs[num_phdrs], 0, sizeof(Elf64_Phdr));
        phdrs[num_phdrs].p_type = PT_GNU_STACK;
        phdrs[num_phdrs].p_flags = PF_R | PF_W;
        phdrs[num_phdrs].p_align = 16;
        num_phdrs++;
    }

    /* compute shstrtab file offset (after all section data) */
    {
        u64 max_end = 0;
        for (i = 0; i < num_out_secs; i++) {
            u64 end;
            if (out_secs[i].shdr.sh_type == SHT_NOBITS) {
                continue;
            }
            end = out_secs[i].shdr.sh_offset + out_secs[i].size;
            if (end > max_end) {
                max_end = end;
            }
        }
        shstrtab_offset = max_end;
    }

    /* section headers follow shstrtab, aligned to 8 */
    sh_offset = shstrtab_offset + (u64)shstrtab_size;
    sh_offset = (sh_offset + 7) & ~(u64)7;

    /* build ELF header */
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_ident[7] = ELFOSABI_NONE;
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = EM_AARCH64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = entry_addr;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_shoff = sh_offset;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = (u16)num_phdrs;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    /* +2 for null section and shstrtab section */
    ehdr.e_shnum = (u16)(num_out_secs + 2);
    ehdr.e_shstrndx = (u16)(num_out_secs + 1);

    /* write output */
    elf_write_exec(output, &ehdr, phdrs, num_phdrs,
                   out_secs, num_out_secs,
                   shstrtab_offset, shstrtab_data, shstrtab_size);

    /* cleanup - free debug/rela/build-id section data owned by out_secs */
    for (i = num_msecs; i < num_out_secs; i++) {
        free(out_secs[i].data);
    }
    free(out_secs);
    free(name_offsets);
    free(shstrtab_data);
    reloc_cleanup();
    layout_free(msecs, num_msecs);
    for (i = 0; i < num_inputs; i++) {
        elf_obj_free(&objs[i]);
    }
    free(objs);
    free((void *)inputs);
    for (i = 0; i < opts.num_defsyms; i++) {
        free(opts.defsym_names[i]);
    }
    free(opts.defsym_names);
    free(opts.defsym_values);

    return 0;
}
