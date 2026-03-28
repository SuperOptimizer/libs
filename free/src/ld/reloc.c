/*
 * reloc.c - Relocation processing for the free linker
 * Resolves symbols and applies AArch64 relocations.
 * Pure C89.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ld_internal.h"

/* ---- symbol resolution ---- */

/*
 * Build a global symbol table from all input objects.
 * Each global_sym tracks the defining object and the final virtual address.
 */
struct global_sym {
    char *name;
    int obj_idx;       /* index into objects array */
    int sym_idx;       /* index into that object's symbol array */
    u64 addr;          /* resolved virtual address */
    int defined;       /* 1 if has a definition */
    int weak;          /* 1 if binding is STB_WEAK */
};

static struct global_sym *g_symtab;
static int g_nsyms;
static int g_symcap;

static void gsym_init(void)
{
    g_symcap = 256;
    g_symtab = (struct global_sym *)calloc(
        (unsigned long)g_symcap, sizeof(struct global_sym));
    if (!g_symtab) {
        fprintf(stderr, "ld: out of memory\n");
        exit(1);
    }
    g_nsyms = 0;
}

static int gsym_find(const char *name)
{
    int i;

    for (i = 0; i < g_nsyms; i++) {
        if (strcmp(g_symtab[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int gsym_add(const char *name, int obj_idx, int sym_idx,
                    int defined, int weak)
{
    int idx;

    if (g_nsyms >= g_symcap) {
        g_symcap *= 2;
        g_symtab = (struct global_sym *)realloc(g_symtab,
            (unsigned long)g_symcap * sizeof(struct global_sym));
        if (!g_symtab) {
            fprintf(stderr, "ld: out of memory\n");
            exit(1);
        }
    }
    idx = g_nsyms++;
    g_symtab[idx].name = (char *)name;
    g_symtab[idx].obj_idx = obj_idx;
    g_symtab[idx].sym_idx = sym_idx;
    g_symtab[idx].addr = 0;
    g_symtab[idx].defined = defined;
    g_symtab[idx].weak = weak;
    return idx;
}

void resolve_symbols(struct elf_obj *objs, int num_objs)
{
    int i;
    int j;

    gsym_init();

    /* first pass: collect all global and weak symbol definitions */
    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_symbols; j++) {
            Elf64_Sym *sym = &objs[i].symbols[j].sym;
            int bind = ELF64_ST_BIND(sym->st_info);
            int is_weak;
            int existing;

            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            if (objs[i].symbols[j].name[0] == '\0') {
                continue;
            }

            is_weak = (bind == STB_WEAK);
            existing = gsym_find(objs[i].symbols[j].name);

            if (sym->st_shndx != SHN_UNDEF) {
                /* definition */
                if (existing >= 0 && g_symtab[existing].defined) {
                    if (!g_symtab[existing].weak && !is_weak) {
                        /* two strong definitions: error */
                        fprintf(stderr,
                            "ld: duplicate symbol '%s' in '%s'"
                            " and '%s'\n",
                            objs[i].symbols[j].name,
                            objs[g_symtab[existing].obj_idx].filename,
                            objs[i].filename);
                        exit(1);
                    }
                    if (g_symtab[existing].weak && !is_weak) {
                        /* new strong beats existing weak */
                        g_symtab[existing].obj_idx = i;
                        g_symtab[existing].sym_idx = j;
                        g_symtab[existing].weak = 0;
                    }
                    /* else: existing is strong or both weak, keep existing */
                } else if (existing >= 0) {
                    g_symtab[existing].obj_idx = i;
                    g_symtab[existing].sym_idx = j;
                    g_symtab[existing].defined = 1;
                    g_symtab[existing].weak = is_weak;
                } else {
                    gsym_add(objs[i].symbols[j].name, i, j, 1, is_weak);
                }
            } else {
                /* reference */
                if (existing < 0) {
                    gsym_add(objs[i].symbols[j].name, i, j, 0, is_weak);
                }
            }
        }
    }

    /* check for undefined symbols (undefined weak symbols resolve to 0) */
    for (i = 0; i < g_nsyms; i++) {
        if (!g_symtab[i].defined && !g_symtab[i].weak) {
            fprintf(stderr, "ld: undefined symbol '%s'\n",
                    g_symtab[i].name);
            exit(1);
        }
    }
}

/*
 * resolve_symbols_shared - like resolve_symbols but allows undefined
 * symbols (they will be resolved at runtime by the dynamic linker).
 */
void resolve_symbols_shared(struct elf_obj *objs, int num_objs)
{
    int i;
    int j;

    gsym_init();

    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_symbols; j++) {
            Elf64_Sym *sym = &objs[i].symbols[j].sym;
            int bind = ELF64_ST_BIND(sym->st_info);
            int is_weak;
            int existing;

            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            if (objs[i].symbols[j].name[0] == '\0') {
                continue;
            }

            is_weak = (bind == STB_WEAK);
            existing = gsym_find(objs[i].symbols[j].name);

            if (sym->st_shndx != SHN_UNDEF) {
                if (existing >= 0 && g_symtab[existing].defined) {
                    if (!g_symtab[existing].weak && !is_weak) {
                        fprintf(stderr,
                            "ld: duplicate symbol '%s' in '%s' "
                            "and '%s'\n",
                            objs[i].symbols[j].name,
                            objs[g_symtab[existing].obj_idx].filename,
                            objs[i].filename);
                        exit(1);
                    }
                    if (g_symtab[existing].weak && !is_weak) {
                        g_symtab[existing].obj_idx = i;
                        g_symtab[existing].sym_idx = j;
                        g_symtab[existing].weak = 0;
                    }
                } else if (existing >= 0) {
                    g_symtab[existing].obj_idx = i;
                    g_symtab[existing].sym_idx = j;
                    g_symtab[existing].defined = 1;
                    g_symtab[existing].weak = is_weak;
                } else {
                    gsym_add(objs[i].symbols[j].name, i, j, 1, is_weak);
                }
            } else {
                if (existing < 0) {
                    gsym_add(objs[i].symbols[j].name, i, j, 0, is_weak);
                }
            }
        }
    }

    /* no undefined check: undefined symbols are allowed in -shared */
}

/*
 * After layout assigns virtual addresses to sections,
 * compute final addresses for all symbols.
 */
void compute_symbol_addrs(struct elf_obj *objs, int num_objs,
                          struct merged_section *msecs, int num_msecs)
{
    int i;
    int j;

    (void)msecs;
    (void)num_msecs;

    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_symbols; j++) {
            Elf64_Sym *sym = &objs[i].symbols[j].sym;
            int sec_idx;
            int k;

            if (sym->st_shndx == SHN_UNDEF || sym->st_shndx == SHN_ABS) {
                continue;
            }

            sec_idx = (int)sym->st_shndx;
            if (sec_idx >= objs[i].num_sections) {
                continue;
            }

            /*
             * Find this input section's contribution in the merged sections.
             * The layout phase stores the resolved vaddr in
             * objs[i].sections[sec_idx].out_addr.
             */
            objs[i].symbols[j].resolved_addr =
                objs[i].sections[sec_idx].out_addr + sym->st_value;

            /* update global symbol table */
            if ((ELF64_ST_BIND(sym->st_info) == STB_GLOBAL ||
                 ELF64_ST_BIND(sym->st_info) == STB_WEAK) &&
                objs[i].symbols[j].name[0] != '\0') {
                k = gsym_find(objs[i].symbols[j].name);
                if (k >= 0 && g_symtab[k].obj_idx == i &&
                    g_symtab[k].sym_idx == j) {
                    g_symtab[k].addr = objs[i].symbols[j].resolved_addr;
                }
            }
        }
    }
}

/*
 * Look up the final virtual address for a symbol used by a relocation.
 * For global symbols, consult the global table.
 * For local/section symbols, use the resolved_addr from the object.
 */
static u64 lookup_sym_addr(struct elf_obj *obj, int sym_idx)
{
    Elf64_Sym *sym;
    int bind;
    int gidx;

    sym = &obj->symbols[sym_idx].sym;
    bind = ELF64_ST_BIND(sym->st_info);

    if ((bind == STB_GLOBAL || bind == STB_WEAK) &&
        obj->symbols[sym_idx].name[0] != '\0') {
        gidx = gsym_find(obj->symbols[sym_idx].name);
        if (gidx >= 0) {
            return g_symtab[gidx].addr;
        }
        fprintf(stderr, "ld: internal error: global '%s' not found\n",
                obj->symbols[sym_idx].name);
        exit(1);
    }

    return obj->symbols[sym_idx].resolved_addr;
}

/* ---- relocation application ---- */

static u32 read32le(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void write32le(u8 *p, u32 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)((v >> 8) & 0xff);
    p[2] = (u8)((v >> 16) & 0xff);
    p[3] = (u8)((v >> 24) & 0xff);
}

static void write16le(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)((v >> 8) & 0xff);
}

static void write64le(u8 *p, u64 v)
{
    write32le(p, (u32)(v & 0xffffffffUL));
    write32le(p + 4, (u32)(v >> 32));
}

static void apply_one_reloc(u8 *data, u64 patch_addr,
                            u32 rtype, u64 sym_addr, i64 addend)
{
    u32 insn;
    i64 val;
    u64 page_s;
    u64 page_p;

    switch (rtype) {
    case R_AARCH64_NONE:
        break;

    case R_AARCH64_ABS64:
        write64le(data, sym_addr + (u64)addend);
        break;

    case R_AARCH64_ABS32:
        write32le(data, (u32)(sym_addr + (u64)addend));
        break;

    case R_AARCH64_PREL32:
        /*
         * 32-bit PC-relative data relocation.
         * *(u32*)P = (S+A-P)
         */
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        write32le(data, (u32)val);
        break;

    case R_AARCH64_PREL16:
        /*
         * 16-bit PC-relative data relocation.
         * *(u16*)P = (S+A-P)
         */
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        write16le(data, (u16)val);
        break;

    case R_AARCH64_MOVW_UABS_G0:
    case R_AARCH64_MOVW_UABS_G0_NC:
        /*
         * MOVZ/MOVK imm16 field: bits [15:0] of (S+A).
         * imm16 is at instruction bits [20:5].
         */
        val = (i64)(sym_addr + (u64)addend) & 0xffff;
        insn = read32le(data);
        insn &= ~(u32)(0xffff << 5);
        insn |= ((u32)val & 0xffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_MOVW_UABS_G1:
    case R_AARCH64_MOVW_UABS_G1_NC:
        /*
         * MOVZ/MOVK imm16 field: bits [31:16] of (S+A).
         */
        val = (i64)((sym_addr + (u64)addend) >> 16) & 0xffff;
        insn = read32le(data);
        insn &= ~(u32)(0xffff << 5);
        insn |= ((u32)val & 0xffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_MOVW_UABS_G2:
    case R_AARCH64_MOVW_UABS_G2_NC:
        /*
         * MOVZ/MOVK imm16 field: bits [47:32] of (S+A).
         */
        val = (i64)((sym_addr + (u64)addend) >> 32) & 0xffff;
        insn = read32le(data);
        insn &= ~(u32)(0xffff << 5);
        insn |= ((u32)val & 0xffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_MOVW_UABS_G3:
        /*
         * MOVZ/MOVK imm16 field: bits [63:48] of (S+A).
         */
        val = (i64)((sym_addr + (u64)addend) >> 48) & 0xffff;
        insn = read32le(data);
        insn &= ~(u32)(0xffff << 5);
        insn |= ((u32)val & 0xffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_ADR_PREL_LO21:
        /*
         * ADR: 21-bit PC-relative.
         * val = S+A-P
         * immhi [23:5] = val[20:2], immlo [30:29] = val[1:0]
         */
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        insn = read32le(data);
        insn &= 0x9f00001fUL;
        insn |= ((u32)val & 0x3) << 29;            /* immlo */
        insn |= (((u32)val >> 2) & 0x7ffff) << 5;  /* immhi */
        write32le(data, insn);
        break;

    case R_AARCH64_ADR_PREL_PG_HI21:
        /*
         * ADRP: 21-bit page-relative.
         * Result = Page(S+A) - Page(P)
         * immhi = bits [23:5], immlo = bits [30:29]
         */
        page_s = (sym_addr + (u64)addend) & ~(u64)0xfff;
        page_p = patch_addr & ~(u64)0xfff;
        val = (i64)(page_s - page_p);
        val >>= 12;
        insn = read32le(data);
        insn &= 0x9f00001fUL;
        insn |= ((u32)val & 0x3) << 29;       /* immlo */
        insn |= (((u32)val >> 2) & 0x7ffff) << 5;  /* immhi */
        write32le(data, insn);
        break;

    case R_AARCH64_ADD_ABS_LO12_NC:
        /*
         * ADD immediate: bits [21:10] = imm12
         * No check (NC) - low 12 bits of address.
         */
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    case R_AARCH64_LDST8_ABS_LO12_NC:
        /*
         * LDR/STR byte: bits [21:10] = imm12 (no shift)
         */
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    case R_AARCH64_TSTBR14:
        /*
         * TBZ/TBNZ: 14-bit PC-relative offset.
         * bits [18:5] = ((S+A-P) >> 2) & 0x3FFF
         */
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        val >>= 2;
        insn = read32le(data);
        insn &= ~(u32)(0x3fff << 5);
        insn |= ((u32)val & 0x3fff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_CONDBR19:
        /*
         * B.cond / CBZ / CBNZ: 19-bit PC-relative offset.
         * bits [23:5] = ((S+A-P) >> 2) & 0x7FFFF
         */
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        val >>= 2;
        insn = read32le(data);
        insn &= ~(u32)(0x7ffff << 5);
        insn |= ((u32)val & 0x7ffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_CALL26:
    case R_AARCH64_JUMP26:
        /*
         * BL/B instruction: bits [25:0] = imm26
         * The offset is (imm26 << 2), PC-relative.
         */
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        val >>= 2;
        insn = read32le(data);
        insn = (insn & 0xfc000000UL) | ((u32)val & 0x03ffffffUL);
        write32le(data, insn);
        break;

    case R_AARCH64_LDST16_ABS_LO12_NC:
        /*
         * LDR/STR halfword: bits [21:10] = imm12
         * The immediate is scaled by 2 (the data size).
         */
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        val >>= 1;
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    case R_AARCH64_LDST32_ABS_LO12_NC:
        /*
         * LDR/STR 32-bit: bits [21:10] = imm12
         * The immediate is scaled by 4 (the data size).
         */
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        val >>= 2;
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    case R_AARCH64_LDST64_ABS_LO12_NC:
        /*
         * LDR/STR 64-bit: bits [21:10] = imm12
         * The immediate is scaled by 8 (the data size).
         */
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        val >>= 3;
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    case R_AARCH64_LDST128_ABS_LO12_NC:
        /*
         * LDR/STR 128-bit (Q register): bits [21:10] = imm12
         * The immediate is scaled by 16 (the data size).
         */
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        val >>= 4;
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    default:
        fprintf(stderr, "ld: unsupported relocation type %u\n", rtype);
        exit(1);
    }
}

void apply_relocations(struct elf_obj *objs, int num_objs)
{
    int i;
    int j;

    (void)num_objs;

    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_relas; j++) {
            struct elf_rela *r = &objs[i].relas[j];
            int sec_idx = r->target_section;
            u32 rtype;
            int sym_idx;
            u64 sym_addr;
            u64 patch_addr;
            u8 *patch_loc;
            struct section *sec;

            if (sec_idx <= 0 || sec_idx >= objs[i].num_sections) {
                continue;
            }

            sec = &objs[i].sections[sec_idx];
            if (!sec->data) {
                continue;
            }

            rtype = (u32)ELF64_R_TYPE(r->rela.r_info);
            sym_idx = (int)ELF64_R_SYM(r->rela.r_info);

            if (sym_idx < 0 || sym_idx >= objs[i].num_symbols) {
                fprintf(stderr, "ld: bad symbol index in relocation\n");
                exit(1);
            }

            sym_addr = lookup_sym_addr(&objs[i], sym_idx);
            patch_addr = sec->out_addr + r->rela.r_offset;
            patch_loc = sec->data + r->rela.r_offset;

            apply_one_reloc(patch_loc, patch_addr,
                            rtype, sym_addr, r->rela.r_addend);
        }
    }
}

/*
 * resolve_symbols_relocatable - like resolve_symbols but allows
 * undefined symbols (they remain undefined in the output .o file
 * for -r / --relocatable mode).
 */
void resolve_symbols_relocatable(struct elf_obj *objs, int num_objs)
{
    int i;
    int j;

    gsym_init();

    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_symbols; j++) {
            Elf64_Sym *sym = &objs[i].symbols[j].sym;
            int bind = ELF64_ST_BIND(sym->st_info);
            int is_weak;
            int existing;

            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            if (objs[i].symbols[j].name[0] == '\0') {
                continue;
            }

            is_weak = (bind == STB_WEAK);
            existing = gsym_find(objs[i].symbols[j].name);

            if (sym->st_shndx != SHN_UNDEF) {
                if (existing >= 0 && g_symtab[existing].defined) {
                    if (!g_symtab[existing].weak && !is_weak) {
                        fprintf(stderr,
                            "ld: duplicate symbol '%s' in '%s' "
                            "and '%s'\n",
                            objs[i].symbols[j].name,
                            objs[g_symtab[existing].obj_idx].filename,
                            objs[i].filename);
                        exit(1);
                    }
                    if (g_symtab[existing].weak && !is_weak) {
                        g_symtab[existing].obj_idx = i;
                        g_symtab[existing].sym_idx = j;
                        g_symtab[existing].weak = 0;
                    }
                } else if (existing >= 0) {
                    g_symtab[existing].obj_idx = i;
                    g_symtab[existing].sym_idx = j;
                    g_symtab[existing].defined = 1;
                    g_symtab[existing].weak = is_weak;
                } else {
                    gsym_add(objs[i].symbols[j].name, i, j, 1, is_weak);
                }
            } else {
                if (existing < 0) {
                    gsym_add(objs[i].symbols[j].name, i, j, 0, is_weak);
                }
            }
        }
    }

    /* no undefined check for relocatable output */
}

u64 find_entry_symbol(const char *name)
{
    int idx;

    idx = gsym_find(name);
    if (idx < 0) {
        fprintf(stderr, "ld: entry symbol '%s' not found\n", name);
        exit(1);
    }
    if (!g_symtab[idx].defined) {
        fprintf(stderr, "ld: entry symbol '%s' is undefined\n", name);
        exit(1);
    }
    return g_symtab[idx].addr;
}

/*
 * find_entry_symbol_safe - like find_entry_symbol but returns 0
 * on failure instead of exiting.
 * Returns 1 if found, 0 if not found.
 */
int find_entry_symbol_safe(const char *name, u64 *addr)
{
    int idx;

    idx = gsym_find(name);
    if (idx < 0 || !g_symtab[idx].defined) {
        *addr = 0;
        return 0;
    }
    *addr = g_symtab[idx].addr;
    return 1;
}

/*
 * add_defsym - define a symbol from the command line (--defsym).
 * Adds as an absolute symbol with the given value.
 */
void add_defsym(const char *name, u64 value)
{
    int existing;

    existing = gsym_find(name);
    if (existing >= 0) {
        /* override existing */
        g_symtab[existing].addr = value;
        g_symtab[existing].defined = 1;
    } else {
        int idx;
        idx = gsym_add(name, -1, -1, 1, 0);
        g_symtab[idx].addr = value;
    }
}

/*
 * check_undefined_symbols - error on any undefined non-weak symbols.
 * Called when --no-undefined is specified.
 */
void check_undefined_symbols(void)
{
    int i;
    int has_undef = 0;

    for (i = 0; i < g_nsyms; i++) {
        if (!g_symtab[i].defined && !g_symtab[i].weak) {
            fprintf(stderr, "ld: undefined symbol '%s'\n",
                    g_symtab[i].name);
            has_undef = 1;
        }
    }
    if (has_undef) {
        exit(1);
    }
}

void reloc_cleanup(void)
{
    free(g_symtab);
    g_symtab = NULL;
    g_nsyms = 0;
    g_symcap = 0;
}

void reloc_get_global_syms(const char ***names, u64 **addrs,
                           int **defined, int **weak,
                           int **obj_idxs, int **sym_idxs,
                           int *count)
{
    int i;
    *count = g_nsyms;
    *names = (const char **)malloc((unsigned long)g_nsyms * sizeof(char *));
    *addrs = (u64 *)malloc((unsigned long)g_nsyms * sizeof(u64));
    *defined = (int *)malloc((unsigned long)g_nsyms * sizeof(int));
    *weak = (int *)malloc((unsigned long)g_nsyms * sizeof(int));
    *obj_idxs = (int *)malloc((unsigned long)g_nsyms * sizeof(int));
    *sym_idxs = (int *)malloc((unsigned long)g_nsyms * sizeof(int));
    for (i = 0; i < g_nsyms; i++) {
        (*names)[i] = g_symtab[i].name;
        (*addrs)[i] = g_symtab[i].addr;
        (*defined)[i] = g_symtab[i].defined;
        (*weak)[i] = g_symtab[i].weak;
        (*obj_idxs)[i] = g_symtab[i].obj_idx;
        (*sym_idxs)[i] = g_symtab[i].sym_idx;
    }
}
