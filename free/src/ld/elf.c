/*
 * elf.c - ELF64 reader/writer for the free linker
 * Reads relocatable object files and writes static executables.
 * Pure C89.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "ld_internal.h"

/* ---- internal helpers ---- */

static void *xmalloc(unsigned long n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "ld: out of memory\n");
        exit(1);
    }
    return p;
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

static char *xstrdup(const char *s)
{
    unsigned long len = strlen(s) + 1;
    char *p = (char *)xmalloc(len);
    memcpy(p, s, len);
    return p;
}

static u8 *read_file(const char *path, unsigned long *out_size)
{
    FILE *f;
    long len;
    u8 *buf;
    unsigned long nread;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ld: cannot open '%s'\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (u8 *)xmalloc((unsigned long)len);
    nread = fread(buf, 1, (unsigned long)len, f);
    if (nread != (unsigned long)len) {
        fprintf(stderr, "ld: short read on '%s'\n", path);
        exit(1);
    }
    fclose(f);
    *out_size = (unsigned long)len;
    return buf;
}

/* ---- ELF reading ---- */

void elf_read(const char *path, struct elf_obj *obj)
{
    u8 *buf;
    unsigned long file_size;
    Elf64_Ehdr *ehdr;
    Elf64_Shdr *shdrs;
    char *shstrtab;
    int i;

    buf = read_file(path, &file_size);
    ehdr = (Elf64_Ehdr *)buf;

    /* validate ELF magic */
    if (file_size < sizeof(Elf64_Ehdr) ||
        ehdr->e_ident[0] != ELFMAG0 ||
        ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3) {
        fprintf(stderr, "ld: '%s' is not an ELF file\n", path);
        exit(1);
    }
    if (ehdr->e_ident[4] != ELFCLASS64) {
        fprintf(stderr, "ld: '%s' is not ELF64\n", path);
        exit(1);
    }
    if (ehdr->e_type != ET_REL) {
        fprintf(stderr, "ld: '%s' is not a relocatable object\n", path);
        exit(1);
    }
    if (ehdr->e_machine != EM_AARCH64) {
        fprintf(stderr, "ld: '%s' is not AArch64\n", path);
        exit(1);
    }

    memcpy(&obj->ehdr, ehdr, sizeof(Elf64_Ehdr));
    obj->filename = xstrdup(path);

    /* read section headers */
    obj->num_sections = (int)ehdr->e_shnum;
    obj->sections = (struct section *)xcalloc(
        (unsigned long)obj->num_sections, sizeof(struct section));

    shdrs = (Elf64_Shdr *)(buf + ehdr->e_shoff);
    for (i = 0; i < obj->num_sections; i++) {
        memcpy(&obj->sections[i].shdr, &shdrs[i], sizeof(Elf64_Shdr));
        obj->sections[i].gc_live = 1;   /* live by default */
        obj->sections[i].out_sec_idx = -1;
    }

    /* read section name string table */
    shstrtab = NULL;
    if (ehdr->e_shstrndx != 0 && ehdr->e_shstrndx < obj->num_sections) {
        Elf64_Shdr *shstr_shdr = &shdrs[ehdr->e_shstrndx];
        shstrtab = (char *)(buf + shstr_shdr->sh_offset);
    }

    /* read section data and names */
    for (i = 0; i < obj->num_sections; i++) {
        Elf64_Shdr *sh = &obj->sections[i].shdr;
        if (shstrtab && sh->sh_name != 0) {
            obj->sections[i].name = xstrdup(shstrtab + sh->sh_name);
        } else {
            obj->sections[i].name = xstrdup("");
        }
        if (sh->sh_type != SHT_NOBITS && sh->sh_size > 0) {
            obj->sections[i].data = (u8 *)xmalloc((unsigned long)sh->sh_size);
            memcpy(obj->sections[i].data, buf + sh->sh_offset,
                   (unsigned long)sh->sh_size);
        } else {
            obj->sections[i].data = NULL;
        }
    }

    /* parse symbol tables and relocations */
    obj->symbols = NULL;
    obj->num_symbols = 0;
    obj->relas = NULL;
    obj->num_relas = 0;

    for (i = 0; i < obj->num_sections; i++) {
        Elf64_Shdr *sh = &obj->sections[i].shdr;

        if (sh->sh_type == SHT_SYMTAB) {
            int nsyms;
            int strtab_idx;
            char *strtab;
            Elf64_Sym *raw_syms;
            int j;

            nsyms = (int)(sh->sh_size / sizeof(Elf64_Sym));
            strtab_idx = (int)sh->sh_link;
            strtab = (char *)obj->sections[strtab_idx].data;
            raw_syms = (Elf64_Sym *)obj->sections[i].data;

            obj->num_symbols = nsyms;
            obj->symbols = (struct elf_sym *)xcalloc(
                (unsigned long)nsyms, sizeof(struct elf_sym));

            for (j = 0; j < nsyms; j++) {
                memcpy(&obj->symbols[j].sym, &raw_syms[j],
                       sizeof(Elf64_Sym));
                if (raw_syms[j].st_name != 0 && strtab) {
                    obj->symbols[j].name =
                        xstrdup(strtab + raw_syms[j].st_name);
                } else {
                    obj->symbols[j].name = xstrdup("");
                }
                obj->symbols[j].resolved_addr = 0;
            }
        }

        if (sh->sh_type == SHT_RELA) {
            int nrelas;
            Elf64_Rela *raw_relas;
            int old_count;
            int j;

            nrelas = (int)(sh->sh_size / sizeof(Elf64_Rela));
            raw_relas = (Elf64_Rela *)obj->sections[i].data;
            old_count = obj->num_relas;

            obj->relas = (struct elf_rela *)realloc(obj->relas,
                (unsigned long)(old_count + nrelas) *
                sizeof(struct elf_rela));
            if (!obj->relas) {
                fprintf(stderr, "ld: out of memory\n");
                exit(1);
            }

            for (j = 0; j < nrelas; j++) {
                struct elf_rela *r = &obj->relas[old_count + j];
                memcpy(&r->rela, &raw_relas[j], sizeof(Elf64_Rela));
                /* sh_info points to the section this rela applies to */
                r->target_section = (int)sh->sh_info;
            }
            obj->num_relas += nrelas;
        }
    }

    free(buf);
}

/* ---- ELF writing ---- */

static void write_bytes(FILE *f, const void *data, unsigned long n)
{
    if (fwrite(data, 1, n, f) != n) {
        fprintf(stderr, "ld: write error\n");
        exit(1);
    }
}

static void write_padding(FILE *f, unsigned long n)
{
    unsigned long i;

    for (i = 0; i < n; i++) {
        fputc(0, f);
    }
}

void elf_write_exec(const char *path,
                    Elf64_Ehdr *ehdr,
                    Elf64_Phdr *phdrs, int num_phdrs,
                    struct out_section *sections, int num_sections,
                    u64 shstrtab_offset, u8 *shstrtab_data,
                    u32 shstrtab_size)
{
    FILE *f;
    int i;
    unsigned long pos;

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "ld: cannot create '%s'\n", path);
        exit(1);
    }

    /* write ELF header */
    write_bytes(f, ehdr, sizeof(Elf64_Ehdr));

    /* write program headers */
    write_bytes(f, phdrs, (unsigned long)num_phdrs * sizeof(Elf64_Phdr));

    /* write section data */
    pos = sizeof(Elf64_Ehdr) +
          (unsigned long)num_phdrs * sizeof(Elf64_Phdr);

    for (i = 0; i < num_sections; i++) {
        unsigned long sec_off;

        if (sections[i].shdr.sh_type == SHT_NOBITS) {
            continue;
        }
        if (sections[i].size == 0) {
            continue;
        }

        sec_off = (unsigned long)sections[i].shdr.sh_offset;
        if (sec_off > pos) {
            write_padding(f, sec_off - pos);
            pos = sec_off;
        }
        write_bytes(f, sections[i].data, (unsigned long)sections[i].size);
        pos += (unsigned long)sections[i].size;
    }

    /* write shstrtab */
    if (shstrtab_data && shstrtab_size > 0) {
        if (shstrtab_offset > pos) {
            write_padding(f, (unsigned long)(shstrtab_offset - pos));
            pos = (unsigned long)shstrtab_offset;
        }
        write_bytes(f, shstrtab_data, (unsigned long)shstrtab_size);
        pos += (unsigned long)shstrtab_size;
    }

    /* write section headers */
    {
        unsigned long sh_off = (unsigned long)ehdr->e_shoff;
        if (sh_off > pos) {
            write_padding(f, sh_off - pos);
        }
        /* null section header */
        {
            Elf64_Shdr null_shdr;
            memset(&null_shdr, 0, sizeof(null_shdr));
            write_bytes(f, &null_shdr, sizeof(Elf64_Shdr));
        }
        for (i = 0; i < num_sections; i++) {
            write_bytes(f, &sections[i].shdr, sizeof(Elf64_Shdr));
        }
        /* shstrtab section header */
        if (shstrtab_data && shstrtab_size > 0) {
            Elf64_Shdr strhdr;
            memset(&strhdr, 0, sizeof(strhdr));
            strhdr.sh_name = 1; /* ".shstrtab" at offset 1 in shstrtab */
            strhdr.sh_type = SHT_STRTAB;
            strhdr.sh_offset = shstrtab_offset;
            strhdr.sh_size = (u64)shstrtab_size;
            strhdr.sh_addralign = 1;
            write_bytes(f, &strhdr, sizeof(Elf64_Shdr));
        }
    }

    fclose(f);

    /* make executable: rwxr-xr-x */
    chmod(path, 0755);
}

void elf_obj_free(struct elf_obj *obj)
{
    int i;

    if (obj->filename) {
        free(obj->filename);
    }
    for (i = 0; i < obj->num_sections; i++) {
        if (obj->sections[i].name) {
            free(obj->sections[i].name);
        }
        if (obj->sections[i].data) {
            free(obj->sections[i].data);
        }
    }
    free(obj->sections);
    for (i = 0; i < obj->num_symbols; i++) {
        if (obj->symbols[i].name) {
            free(obj->symbols[i].name);
        }
    }
    free(obj->symbols);
    free(obj->relas);
}
