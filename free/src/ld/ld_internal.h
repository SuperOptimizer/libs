/*
 * ld_internal.h - Shared data structures for the free linker
 * Pure C89.
 */
#ifndef LD_INTERNAL_H
#define LD_INTERNAL_H

#include "elf.h"

/* ---- parsed input section ---- */
struct section {
    Elf64_Shdr shdr;
    char *name;
    u8 *data;
    u64 out_addr;   /* virtual address assigned during layout */
    int gc_live;    /* gc-sections: 1 if reachable, 0 if dead */
    int out_sec_idx; /* index into merged output section, or -1 */
};

/* ---- parsed symbol ---- */
struct elf_sym {
    Elf64_Sym sym;
    char *name;
    u64 resolved_addr;
};

/* ---- parsed relocation ---- */
struct elf_rela {
    Elf64_Rela rela;
    int target_section;   /* section index this relocation applies to */
};

/* ---- parsed object file ---- */
struct elf_obj {
    Elf64_Ehdr ehdr;
    char *filename;
    struct section *sections;
    int num_sections;
    struct elf_sym *symbols;
    int num_symbols;
    struct elf_rela *relas;
    int num_relas;
};

/* ---- input contribution to a merged section ---- */
struct input_piece {
    int obj_idx;
    int sec_idx;
    u64 offset_in_merged;
};

/* ---- merged output section ---- */
struct merged_section {
    Elf64_Shdr shdr;
    const char *name;
    u8 *data;
    unsigned long size;
    unsigned long capacity;
    struct input_piece *inputs;
    int num_inputs;
    int input_cap;
};

/* ---- output section for the writer ---- */
struct out_section {
    Elf64_Shdr shdr;
    u8 *data;
    unsigned long size;
};

/* ---- linker options ---- */
struct ld_options {
    int pie;                /* -pie: position independent executable */
    int static_mode;        /* -static: force static linking */
    int no_undefined;       /* --no-undefined: error on undef syms */
    int sort_section;       /* --sort-section=name */
    int z_noexecstack;      /* -z noexecstack */
    int z_relro;            /* -z relro */
    int z_now;              /* -z now (full RELRO) */
    u64 z_max_page_size;    /* -z max-page-size=N, 0 = default */
    /* --defsym entries */
    char **defsym_names;
    u64 *defsym_values;
    int num_defsyms;
    int defsym_cap;
    /* --version-script */
    const char *version_script;
    int strip_debug;        /* --strip-debug: remove .debug_* sections */
    int strip_all;          /* --strip-all / -s: remove symbols and debug */
    int fatal_warnings;     /* --fatal-warnings: treat warnings as errors */
};

/* ---- elf.c ---- */
void elf_read(const char *path, struct elf_obj *obj);
void elf_write_exec(const char *path,
                    Elf64_Ehdr *ehdr,
                    Elf64_Phdr *phdrs, int num_phdrs,
                    struct out_section *sections, int num_sections,
                    u64 shstrtab_offset, u8 *shstrtab_data,
                    u32 shstrtab_size);
void elf_obj_free(struct elf_obj *obj);

/* ---- reloc.c ---- */
void resolve_symbols(struct elf_obj *objs, int num_objs);
void resolve_symbols_relocatable(struct elf_obj *objs, int num_objs);
void compute_symbol_addrs(struct elf_obj *objs, int num_objs,
                          struct merged_section *msecs, int num_msecs);
void apply_relocations(struct elf_obj *objs, int num_objs);
u64  find_entry_symbol(const char *name);
int  find_entry_symbol_safe(const char *name, u64 *addr);
void reloc_cleanup(void);
void reloc_get_global_syms(const char ***names, u64 **addrs,
                           int **defined, int **weak,
                           int **obj_idxs, int **sym_idxs,
                           int *count);

/* ---- layout.c ---- */
void layout_sections(struct elf_obj *objs, int num_objs,
                     struct merged_section **out_msecs, int *out_num_msecs,
                     Elf64_Phdr *out_phdrs, int *out_num_phdrs);
void layout_free(struct merged_section *msecs, int num_msecs);

/* gc-sections: mark reachable sections, discard unreachable */
void gc_sections(struct elf_obj *objs, int num_objs,
                 const char *entry_name);

/* ---- dynamic.c ---- */
void dyn_init(void);
void dyn_add_symbol(const char *name, u64 addr, int type, int bind,
                    u16 shndx, u64 size);
void dyn_collect_globals(struct elf_obj *objs, int num_objs);
void dyn_build_sections(u64 base_addr, const char *soname);
void dyn_patch_addrs(u64 dynsym_addr, u64 dynstr_addr,
                     u64 hash_addr, u64 got_addr,
                     u64 rela_addr, u64 dynamic_addr,
                     const char *soname);
u8  *dyn_get_dynsym(u32 *size);
u8  *dyn_get_dynstr(u32 *size);
u8  *dyn_get_hash(u32 *size);
u8  *dyn_get_dynamic(u32 *size);
u8  *dyn_get_got(u32 *size);
u8  *dyn_get_rela_dyn(u32 *size);
void dyn_set_bind_now(int enable);
void dyn_cleanup(void);

/* ---- reloc.c (shared mode) ---- */
void resolve_symbols_shared(struct elf_obj *objs, int num_objs);

/* ---- reloc.c (defsym support) ---- */
void add_defsym(const char *name, u64 value);

/* ---- reloc.c (no-undefined check for shared/pie) ---- */
void check_undefined_symbols(void);

/* ---- script.c (linker script parser and executor) ---- */
struct ld_script;
struct ld_script *script_read(const char *path);
struct ld_script *script_parse(const char *data, unsigned long size);
void script_layout(struct ld_script *script,
                   struct elf_obj *objs, int num_objs,
                   struct merged_section **out_msecs, int *out_num_msecs,
                   Elf64_Phdr *out_phdrs, int *out_num_phdrs);
void script_free(struct ld_script *sc);
int  script_glob_match(const char *pat, const char *str);
/* Returns the ENTRY symbol name from the script, or NULL if none set */
const char *script_entry(struct ld_script *sc);

#endif /* LD_INTERNAL_H */
