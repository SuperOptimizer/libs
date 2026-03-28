/*
 * dynamic.c - Dynamic linking support for the free linker.
 * Generates GOT, PLT, .dynamic, .dynsym, .dynstr, .hash sections
 * for shared library output (ET_DYN).
 * Pure C89.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ld_internal.h"

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

static void *xcalloc(unsigned long count, unsigned long size)
{
    void *p = calloc(count, size);
    if (!p) {
        fprintf(stderr, "ld: out of memory\n");
        exit(1);
    }
    return p;
}

/* ---- dynamic symbol collection ---- */

struct dyn_sym_entry {
    char *name;
    u64 addr;
    int type;       /* STT_FUNC, STT_NOTYPE, etc. */
    int bind;       /* STB_GLOBAL, STB_LOCAL */
    u16 shndx;
    u64 size;
};

struct dyn_state {
    struct dyn_sym_entry *syms;
    int nsyms;
    int sym_cap;

    /* built sections */
    u8 *dynsym_data;
    u32 dynsym_size;

    u8 *dynstr_data;
    u32 dynstr_size;

    u8 *hash_data;
    u32 hash_size;

    u8 *dynamic_data;
    u32 dynamic_size;

    u8 *got_data;
    u32 got_size;
    int got_nentries;

    u8 *plt_data;
    u32 plt_size;

    u8 *rela_dyn_data;
    u32 rela_dyn_size;
    int rela_dyn_count;

    int bind_now;       /* DT_BIND_NOW / DF_BIND_NOW */
};

static struct dyn_state g_dyn;

void dyn_init(void)
{
    memset(&g_dyn, 0, sizeof(g_dyn));
    g_dyn.sym_cap = 64;
    g_dyn.syms = (struct dyn_sym_entry *)xcalloc(
        (unsigned long)g_dyn.sym_cap, sizeof(struct dyn_sym_entry));
    g_dyn.nsyms = 0;
}

void dyn_add_symbol(const char *name, u64 addr, int type, int bind,
                    u16 shndx, u64 size)
{
    struct dyn_sym_entry *e;

    if (g_dyn.nsyms >= g_dyn.sym_cap) {
        g_dyn.sym_cap *= 2;
        g_dyn.syms = (struct dyn_sym_entry *)realloc(g_dyn.syms,
            (unsigned long)g_dyn.sym_cap *
            sizeof(struct dyn_sym_entry));
        if (!g_dyn.syms) {
            fprintf(stderr, "ld: out of memory\n");
            exit(1);
        }
    }
    e = &g_dyn.syms[g_dyn.nsyms++];
    e->name = (char *)xmalloc(strlen(name) + 1);
    strcpy(e->name, name);
    e->addr = addr;
    e->type = type;
    e->bind = bind;
    e->shndx = shndx;
    e->size = size;
}

/* ---- ELF hash function (SYSV) ---- */

static u32 elf_hash(const char *name)
{
    u32 h = 0;
    u32 g;
    const unsigned char *p = (const unsigned char *)name;

    while (*p) {
        h = (h << 4) + *p++;
        g = h & 0xf0000000UL;
        if (g) {
            h ^= g >> 24;
        }
        h &= ~g;
    }
    return h;
}

/* ---- build .dynstr ---- */

static void build_dynstr(void)
{
    u32 cap;
    u32 pos;
    int i;

    cap = 256;
    g_dyn.dynstr_data = (u8 *)xmalloc(cap);

    /* byte 0: null */
    g_dyn.dynstr_data[0] = '\0';
    pos = 1;

    for (i = 0; i < g_dyn.nsyms; i++) {
        u32 len = (u32)(strlen(g_dyn.syms[i].name) + 1);
        if (pos + len > cap) {
            while (pos + len > cap) {
                cap *= 2;
            }
            g_dyn.dynstr_data = (u8 *)realloc(g_dyn.dynstr_data, cap);
            if (!g_dyn.dynstr_data) {
                fprintf(stderr, "ld: out of memory\n");
                exit(1);
            }
        }
        memcpy(g_dyn.dynstr_data + pos, g_dyn.syms[i].name, len);
        pos += len;
    }

    g_dyn.dynstr_size = pos;
}

/* ---- build .dynsym ---- */

static void build_dynsym(void)
{
    int total;
    Elf64_Sym *syms;
    int i;
    u32 str_pos;

    /* entry 0 is the null symbol */
    total = 1 + g_dyn.nsyms;
    syms = (Elf64_Sym *)xcalloc((unsigned long)total, sizeof(Elf64_Sym));

    /* null symbol */
    memset(&syms[0], 0, sizeof(Elf64_Sym));

    /* locate name in dynstr for each symbol */
    str_pos = 1; /* after the initial null byte */
    for (i = 0; i < g_dyn.nsyms; i++) {
        Elf64_Sym *s = &syms[i + 1];
        s->st_name = str_pos;
        s->st_info = ELF64_ST_INFO(g_dyn.syms[i].bind,
                                    g_dyn.syms[i].type);
        s->st_other = STV_DEFAULT;
        s->st_shndx = g_dyn.syms[i].shndx;
        s->st_value = g_dyn.syms[i].addr;
        s->st_size = g_dyn.syms[i].size;

        str_pos += (u32)(strlen(g_dyn.syms[i].name) + 1);
    }

    g_dyn.dynsym_size = (u32)((unsigned long)total * sizeof(Elf64_Sym));
    g_dyn.dynsym_data = (u8 *)syms;
}

/* ---- build .hash ---- */

static void build_hash(void)
{
    u32 nbucket;
    u32 nchain;
    u32 *buf;
    u32 total_words;
    int i;

    nchain = (u32)(1 + g_dyn.nsyms); /* includes null sym at index 0 */
    nbucket = nchain > 1 ? nchain : 1;

    total_words = 2 + nbucket + nchain;
    buf = (u32 *)xcalloc((unsigned long)total_words, sizeof(u32));

    buf[0] = nbucket;
    buf[1] = nchain;

    /* all chains initialized to STN_UNDEF (0) by xcalloc */

    /* build bucket chains */
    {
        u32 str_pos;
        str_pos = 1;
        for (i = 0; i < g_dyn.nsyms; i++) {
            u32 h = elf_hash(g_dyn.syms[i].name) % nbucket;
            u32 sym_idx = (u32)(i + 1);
            u32 *chain = buf + 2 + nbucket;

            /* insert at head of chain */
            chain[sym_idx] = buf[2 + h];
            buf[2 + h] = sym_idx;

            str_pos += (u32)(strlen(g_dyn.syms[i].name) + 1);
        }
    }

    g_dyn.hash_size = total_words * (u32)sizeof(u32);
    g_dyn.hash_data = (u8 *)buf;
}

/* ---- build .got ---- */

static void build_got(void)
{
    /*
     * GOT layout:
     *   [0] = address of .dynamic (filled at link time)
     *   [1] = 0 (reserved for ld.so)
     *   [2] = 0 (reserved for ld.so)
     *   [3..] = one entry per dynamic symbol
     */
    int nentries;
    u64 *entries;

    nentries = 3 + g_dyn.nsyms;
    entries = (u64 *)xcalloc((unsigned long)nentries, sizeof(u64));

    /* entries [1] and [2] will be filled by the dynamic linker at runtime */
    /* entries [3..] will be filled with symbol addresses by ld.so or
     * with the symbol addresses for internal symbols now */

    g_dyn.got_nentries = nentries;
    g_dyn.got_size = (u32)((unsigned long)nentries * sizeof(u64));
    g_dyn.got_data = (u8 *)entries;
}

/* ---- build .rela.dyn (GOT relocations) ---- */

static void build_rela_dyn(u64 got_vaddr)
{
    Elf64_Rela *relas;
    int i;

    relas = (Elf64_Rela *)xcalloc(
        (unsigned long)g_dyn.nsyms, sizeof(Elf64_Rela));

    for (i = 0; i < g_dyn.nsyms; i++) {
        u64 got_entry_addr = got_vaddr + (u64)(3 + i) * 8;
        u32 sym_idx = (u32)(i + 1);

        relas[i].r_offset = got_entry_addr;
        if (g_dyn.syms[i].shndx != SHN_UNDEF) {
            /* defined symbol: R_AARCH64_RELATIVE */
            relas[i].r_info = ELF64_R_INFO(0, R_AARCH64_GLOB_DAT);
            relas[i].r_addend = 0;
            relas[i].r_info = ELF64_R_INFO(sym_idx,
                                            R_AARCH64_GLOB_DAT);
        } else {
            relas[i].r_info = ELF64_R_INFO(sym_idx,
                                            R_AARCH64_GLOB_DAT);
        }
        relas[i].r_addend = 0;
    }

    g_dyn.rela_dyn_count = g_dyn.nsyms;
    g_dyn.rela_dyn_size = (u32)((unsigned long)g_dyn.nsyms *
                                sizeof(Elf64_Rela));
    g_dyn.rela_dyn_data = (u8 *)relas;
}

/* ---- build .dynamic ---- */

static void build_dynamic(u64 dynstr_addr, u64 dynsym_addr,
                          u64 hash_addr, u64 got_addr,
                          u64 rela_addr, u32 rela_size,
                          const char *soname)
{
    Elf64_Dyn *dyn;
    int ndyn;
    u32 soname_off;

    ndyn = 0;
    dyn = (Elf64_Dyn *)xcalloc(24, sizeof(Elf64_Dyn));

    /* DT_SONAME - find in dynstr (add if needed) */
    if (soname && soname[0] != '\0') {
        /* find soname in dynstr by searching */
        soname_off = 0;
        {
            u32 pos = 1;
            int j;
            int found = 0;
            for (j = 0; j < g_dyn.nsyms; j++) {
                if (strcmp(g_dyn.syms[j].name, soname) == 0) {
                    soname_off = pos;
                    found = 1;
                    break;
                }
                pos += (u32)(strlen(g_dyn.syms[j].name) + 1);
            }
            if (!found) {
                /* append soname to dynstr */
                u32 len = (u32)(strlen(soname) + 1);
                soname_off = g_dyn.dynstr_size;
                g_dyn.dynstr_data = (u8 *)realloc(
                    g_dyn.dynstr_data,
                    (unsigned long)(g_dyn.dynstr_size + len));
                memcpy(g_dyn.dynstr_data + g_dyn.dynstr_size,
                       soname, len);
                g_dyn.dynstr_size += len;
            }
        }
        dyn[ndyn].d_tag = DT_SONAME;
        dyn[ndyn].d_un.d_val = soname_off;
        ndyn++;
    }

    dyn[ndyn].d_tag = DT_HASH;
    dyn[ndyn].d_un.d_ptr = hash_addr;
    ndyn++;

    dyn[ndyn].d_tag = DT_STRTAB;
    dyn[ndyn].d_un.d_ptr = dynstr_addr;
    ndyn++;

    dyn[ndyn].d_tag = DT_SYMTAB;
    dyn[ndyn].d_un.d_ptr = dynsym_addr;
    ndyn++;

    dyn[ndyn].d_tag = DT_STRSZ;
    dyn[ndyn].d_un.d_val = g_dyn.dynstr_size;
    ndyn++;

    dyn[ndyn].d_tag = DT_SYMENT;
    dyn[ndyn].d_un.d_val = sizeof(Elf64_Sym);
    ndyn++;

    dyn[ndyn].d_tag = DT_PLTGOT;
    dyn[ndyn].d_un.d_ptr = got_addr;
    ndyn++;

    if (rela_size > 0) {
        dyn[ndyn].d_tag = DT_RELA;
        dyn[ndyn].d_un.d_ptr = rela_addr;
        ndyn++;

        dyn[ndyn].d_tag = DT_RELASZ;
        dyn[ndyn].d_un.d_val = rela_size;
        ndyn++;

        dyn[ndyn].d_tag = DT_RELAENT;
        dyn[ndyn].d_un.d_val = sizeof(Elf64_Rela);
        ndyn++;
    }

    /* DT_FLAGS for BIND_NOW (set externally via dyn_set_bind_now) */
    if (g_dyn.bind_now) {
        dyn[ndyn].d_tag = DT_BIND_NOW;
        dyn[ndyn].d_un.d_val = 0;
        ndyn++;

        dyn[ndyn].d_tag = DT_FLAGS;
        dyn[ndyn].d_un.d_val = DF_BIND_NOW;
        ndyn++;

        dyn[ndyn].d_tag = DT_FLAGS_1;
        dyn[ndyn].d_un.d_val = DF_1_NOW;
        ndyn++;
    }

    /* DT_NULL terminator */
    dyn[ndyn].d_tag = DT_NULL;
    dyn[ndyn].d_un.d_val = 0;
    ndyn++;

    g_dyn.dynamic_size = (u32)((unsigned long)ndyn * sizeof(Elf64_Dyn));
    g_dyn.dynamic_data = (u8 *)dyn;
}

/* ---- PLT stub for aarch64 ---- */

/*
 * PLT entry for aarch64 (each entry is 16 bytes):
 *   adrp x16, GOT_entry_page
 *   ldr  x17, [x16, :lo12:GOT_entry]
 *   br   x17
 *   nop
 *
 * We don't generate a PLT header (PLT[0]) for the minimal case.
 * For internal symbols in a .so, PLT is not needed since we use
 * direct calls. PLT is needed for calls to external symbols not
 * defined in this .so. For minimum viability, we generate GOT
 * entries for exported symbols but skip PLT (direct BL works
 * within the .so, and the dynamic linker patches GOT entries).
 */

/* ---- public interface ---- */

void dyn_collect_globals(struct elf_obj *objs, int num_objs)
{
    int i;
    int j;

    dyn_init();

    for (i = 0; i < num_objs; i++) {
        for (j = 0; j < objs[i].num_symbols; j++) {
            Elf64_Sym *sym = &objs[i].symbols[j].sym;
            int bind = ELF64_ST_BIND(sym->st_info);
            int type = ELF64_ST_TYPE(sym->st_info);

            if (bind != STB_GLOBAL) {
                continue;
            }
            if (objs[i].symbols[j].name[0] == '\0') {
                continue;
            }
            if (sym->st_shndx == SHN_UNDEF) {
                continue;
            }

            dyn_add_symbol(objs[i].symbols[j].name,
                           objs[i].symbols[j].resolved_addr,
                           type, bind, 1, /* shndx=1 placeholder */
                           sym->st_size);
        }
    }
}

void dyn_build_sections(u64 base_addr, const char *soname)
{
    (void)base_addr;

    /*
     * Build all dynamic sections. The actual addresses will be
     * assigned during layout. For now, build the data and compute
     * sizes. The addresses stored in .dynamic entries will be
     * patched after layout.
     */

    build_dynstr();
    build_dynsym();
    build_hash();
    build_got();

    /*
     * Placeholder addresses - will be patched in dyn_patch_addrs.
     * Use 0 for now.
     */
    build_rela_dyn(0);
    build_dynamic(0, 0, 0, 0, 0, g_dyn.rela_dyn_size, soname);
}

void dyn_patch_addrs(u64 dynsym_addr, u64 dynstr_addr,
                     u64 hash_addr, u64 got_addr,
                     u64 rela_addr, u64 dynamic_addr,
                     const char *soname)
{
    int i;
    u64 *got_entries;

    /* patch GOT[0] = .dynamic address */
    got_entries = (u64 *)g_dyn.got_data;
    got_entries[0] = dynamic_addr;

    /* patch GOT entries for symbols */
    for (i = 0; i < g_dyn.nsyms; i++) {
        got_entries[3 + i] = g_dyn.syms[i].addr;
    }

    /* patch .rela.dyn offsets */
    {
        Elf64_Rela *relas = (Elf64_Rela *)g_dyn.rela_dyn_data;
        for (i = 0; i < g_dyn.rela_dyn_count; i++) {
            relas[i].r_offset = got_addr + (u64)(3 + i) * 8;
        }
    }

    /* rebuild .dynamic with correct addresses */
    free(g_dyn.dynamic_data);
    g_dyn.dynamic_data = NULL;
    g_dyn.dynamic_size = 0;
    build_dynamic(dynstr_addr, dynsym_addr, hash_addr, got_addr,
                  rela_addr, g_dyn.rela_dyn_size, soname);
}

/* ---- getters for linker ---- */

u8 *dyn_get_dynsym(u32 *size)
{
    *size = g_dyn.dynsym_size;
    return g_dyn.dynsym_data;
}

u8 *dyn_get_dynstr(u32 *size)
{
    *size = g_dyn.dynstr_size;
    return g_dyn.dynstr_data;
}

u8 *dyn_get_hash(u32 *size)
{
    *size = g_dyn.hash_size;
    return g_dyn.hash_data;
}

u8 *dyn_get_dynamic(u32 *size)
{
    *size = g_dyn.dynamic_size;
    return g_dyn.dynamic_data;
}

u8 *dyn_get_got(u32 *size)
{
    *size = g_dyn.got_size;
    return g_dyn.got_data;
}

u8 *dyn_get_rela_dyn(u32 *size)
{
    *size = g_dyn.rela_dyn_size;
    return g_dyn.rela_dyn_data;
}

void dyn_set_bind_now(int enable)
{
    g_dyn.bind_now = enable;
}

void dyn_cleanup(void)
{
    int i;

    for (i = 0; i < g_dyn.nsyms; i++) {
        free(g_dyn.syms[i].name);
    }
    free(g_dyn.syms);
    free(g_dyn.dynsym_data);
    free(g_dyn.dynstr_data);
    free(g_dyn.hash_data);
    free(g_dyn.dynamic_data);
    free(g_dyn.got_data);
    free(g_dyn.rela_dyn_data);
    memset(&g_dyn, 0, sizeof(g_dyn));
}
