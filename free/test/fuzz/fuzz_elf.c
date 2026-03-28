/*
 * fuzz_elf.c - ELF reader fuzzer for the free linker.
 * Feeds arbitrary binary input to the ELF parser.
 * No input should cause a crash: truncated headers, bad magic,
 * corrupt section headers, etc. should all be handled gracefully.
 * Reads from a file argument or stdin (for use with AFL/libfuzzer).
 *
 * Build (standalone):
 *   cc -std=c89 -I../../include -I../../src/ld -o fuzz_elf \
 *      fuzz_elf.c
 *
 * Pure C89. All variables at top of block.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "elf.h"

/*
 * We re-implement a safe version of the ELF parser here rather than
 * linking against elf.c directly, because elf.c calls exit() on errors
 * and reads from disk via a path. We want to parse from a memory buffer
 * and recover from all errors.
 */

static jmp_buf fuzz_jmp;

static void fuzz_die(const char *msg)
{
    (void)msg;
    longjmp(fuzz_jmp, 1);
}

/*
 * Safe ELF parser: validates all offsets and sizes against the buffer
 * bounds before accessing any data. This is the kind of hardening we
 * want in the real linker too.
 */
static void fuzz_parse_elf(const u8 *buf, unsigned long size)
{
    Elf64_Ehdr ehdr;
    u16 shnum;
    u16 shstrndx;
    u64 shoff;
    unsigned long shdrs_end;
    Elf64_Shdr shdr;
    unsigned long i;
    u64 sh_offset;
    u64 sh_size;
    char *shstrtab;
    unsigned long shstrtab_off;
    unsigned long shstrtab_size;

    /* check minimum size for ELF header */
    if (size < sizeof(Elf64_Ehdr)) {
        fuzz_die("too small for ELF header");
        return;
    }

    memcpy(&ehdr, buf, sizeof(Elf64_Ehdr));

    /* validate ELF magic */
    if (ehdr.e_ident[0] != ELFMAG0 ||
        ehdr.e_ident[1] != ELFMAG1 ||
        ehdr.e_ident[2] != ELFMAG2 ||
        ehdr.e_ident[3] != ELFMAG3) {
        fuzz_die("bad ELF magic");
        return;
    }

    /* check class (we handle ELF64 but don't crash on ELF32) */
    if (ehdr.e_ident[4] != ELFCLASS64) {
        fuzz_die("not ELF64");
        return;
    }

    /* read section header count and string table index */
    shnum = ehdr.e_shnum;
    shstrndx = ehdr.e_shstrndx;
    shoff = ehdr.e_shoff;

    /* validate section header table bounds */
    if (shnum == 0) {
        return; /* no sections, that's valid */
    }

    if (shoff > size) {
        fuzz_die("shoff past end of file");
        return;
    }

    shdrs_end = (unsigned long)shoff +
                (unsigned long)shnum * sizeof(Elf64_Shdr);
    if (shdrs_end > size || shdrs_end < (unsigned long)shoff) {
        fuzz_die("section headers extend past end of file");
        return;
    }

    /* read and validate each section header */
    shstrtab = NULL;
    shstrtab_off = 0;
    shstrtab_size = 0;

    /* first, try to find shstrtab */
    if (shstrndx < shnum) {
        memcpy(&shdr, buf + shoff + (unsigned long)shstrndx * sizeof(Elf64_Shdr),
               sizeof(Elf64_Shdr));
        if (shdr.sh_type == SHT_STRTAB) {
            shstrtab_off = (unsigned long)shdr.sh_offset;
            shstrtab_size = (unsigned long)shdr.sh_size;
            if (shstrtab_off + shstrtab_size <= size &&
                shstrtab_off + shstrtab_size >= shstrtab_off) {
                shstrtab = (char *)(buf + shstrtab_off);
            }
        }
    }

    /* iterate through all section headers */
    for (i = 0; i < (unsigned long)shnum; i++) {
        memcpy(&shdr, buf + shoff + i * sizeof(Elf64_Shdr),
               sizeof(Elf64_Shdr));

        /* validate section data bounds */
        sh_offset = shdr.sh_offset;
        sh_size = shdr.sh_size;

        if (shdr.sh_type != SHT_NOBITS && sh_size > 0) {
            if ((unsigned long)sh_offset + (unsigned long)sh_size > size ||
                (unsigned long)sh_offset + (unsigned long)sh_size <
                    (unsigned long)sh_offset) {
                continue; /* skip corrupt section, don't crash */
            }
        }

        /* try to read section name from shstrtab */
        if (shstrtab != NULL && shdr.sh_name < (u32)shstrtab_size) {
            const char *name;
            int name_len;

            name = shstrtab + shdr.sh_name;
            /* ensure null termination within bounds */
            name_len = 0;
            while (shdr.sh_name + (u32)name_len < (u32)shstrtab_size &&
                   name[name_len] != '\0') {
                name_len++;
                if (name_len > 256) {
                    break; /* safety limit */
                }
            }
            /* name is valid, consume it (don't crash) */
            (void)name_len;
        }

        /* if it's a symbol table, try to parse symbols */
        if (shdr.sh_type == SHT_SYMTAB && sh_size > 0) {
            unsigned long nsyms;
            unsigned long j;
            Elf64_Sym sym;

            if ((unsigned long)sh_offset + (unsigned long)sh_size > size) {
                continue;
            }

            nsyms = (unsigned long)sh_size / sizeof(Elf64_Sym);
            for (j = 0; j < nsyms && j < 10000; j++) {
                unsigned long sym_off;

                sym_off = (unsigned long)sh_offset + j * sizeof(Elf64_Sym);
                if (sym_off + sizeof(Elf64_Sym) > size) {
                    break;
                }
                memcpy(&sym, buf + sym_off, sizeof(Elf64_Sym));
                /* access fields to exercise the parser */
                (void)ELF64_ST_BIND(sym.st_info);
                (void)ELF64_ST_TYPE(sym.st_info);
                (void)sym.st_value;
                (void)sym.st_size;
                (void)sym.st_shndx;
            }
        }

        /* if it's a relocation table, try to parse relocations */
        if (shdr.sh_type == SHT_RELA && sh_size > 0) {
            unsigned long nrelas;
            unsigned long j;
            Elf64_Rela rela;

            if ((unsigned long)sh_offset + (unsigned long)sh_size > size) {
                continue;
            }

            nrelas = (unsigned long)sh_size / sizeof(Elf64_Rela);
            for (j = 0; j < nrelas && j < 10000; j++) {
                unsigned long rela_off;

                rela_off = (unsigned long)sh_offset + j * sizeof(Elf64_Rela);
                if (rela_off + sizeof(Elf64_Rela) > size) {
                    break;
                }
                memcpy(&rela, buf + rela_off, sizeof(Elf64_Rela));
                /* exercise the relocation parsing */
                (void)ELF64_R_SYM(rela.r_info);
                (void)ELF64_R_TYPE(rela.r_info);
                (void)rela.r_addend;
            }
        }
    }

    /* try to read program headers if present */
    if (ehdr.e_phnum > 0 && ehdr.e_phoff > 0) {
        unsigned long ph_end;

        ph_end = (unsigned long)ehdr.e_phoff +
                 (unsigned long)ehdr.e_phnum * sizeof(Elf64_Phdr);
        if (ph_end <= size && ph_end >= (unsigned long)ehdr.e_phoff) {
            Elf64_Phdr phdr;
            unsigned long j;

            for (j = 0; j < (unsigned long)ehdr.e_phnum; j++) {
                unsigned long ph_off;

                ph_off = (unsigned long)ehdr.e_phoff +
                         j * sizeof(Elf64_Phdr);
                if (ph_off + sizeof(Elf64_Phdr) > size) {
                    break;
                }
                memcpy(&phdr, buf + ph_off, sizeof(Elf64_Phdr));
                (void)phdr.p_type;
                (void)phdr.p_flags;
                (void)phdr.p_vaddr;
                (void)phdr.p_filesz;
                (void)phdr.p_memsz;
            }
        }
    }
}

/* ---- input reading ---- */

static u8 *read_input(FILE *f, unsigned long *out_len)
{
    u8 *buf;
    unsigned long len;
    unsigned long cap;
    int ch;

    cap = 4096;
    buf = (u8 *)malloc(cap);
    if (!buf) {
        return NULL;
    }
    len = 0;

    while ((ch = fgetc(f)) != EOF) {
        if (len + 2 >= cap) {
            cap *= 2;
            buf = (u8 *)realloc(buf, cap);
            if (!buf) {
                return NULL;
            }
        }
        buf[len++] = (u8)ch;
    }
    *out_len = len;
    return buf;
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    FILE *f;
    u8 *input;
    unsigned long input_len;

    /* open input */
    if (argc > 1) {
        f = fopen(argv[1], "rb");
        if (!f) {
            return 0;
        }
    } else {
        f = stdin;
    }

    input = read_input(f, &input_len);
    if (f != stdin) {
        fclose(f);
    }
    if (!input) {
        return 0;
    }

    /* set up error recovery */
    if (setjmp(fuzz_jmp) != 0) {
        free(input);
        return 0;
    }

    /* parse the ELF data */
    fuzz_parse_elf(input, input_len);

    free(input);
    return 0;
}
