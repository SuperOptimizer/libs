/*
 * elf.h - ELF64 structures for the free toolchain
 * Minimal definitions needed for aarch64 object files and executables
 */
#ifndef FREE_ELF_H
#define FREE_ELF_H

#include "free.h"

/* ---- ELF identification ---- */
#define EI_NIDENT    16
#define ELFMAG0      0x7f
#define ELFMAG1      'E'
#define ELFMAG2      'L'
#define ELFMAG3      'F'
#define ELFCLASS64   2
#define ELFDATA2LSB  1
#define EV_CURRENT   1
#define ELFOSABI_NONE 0

/* ---- ELF types ---- */
#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2
#define ET_DYN    3

/* ---- Machine ---- */
#define EM_AARCH64 183

/* ---- Section header types ---- */
#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_HASH     5
#define SHT_DYNAMIC  6
#define SHT_NOTE     7
#define SHT_NOBITS   8
#define SHT_REL      9
#define SHT_DYNSYM   11
#define SHT_INIT_ARRAY  14
#define SHT_FINI_ARRAY  15
#define SHT_PREINIT_ARRAY 16

/* ---- Section header flags ---- */
#define SHF_WRITE     0x1
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4
#define SHF_MERGE     0x10
#define SHF_STRINGS   0x20
#define SHF_INFO_LINK 0x40
#define SHF_GROUP     0x200
#define SHF_TLS       0x400

/* ---- Symbol visibility ---- */
#define STV_DEFAULT   0
#define STV_HIDDEN    2

/* ---- Symbol binding ---- */
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

/* ---- Symbol types ---- */
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4

/* ---- Symbol visibility ---- */
#define ELF64_ST_VISIBILITY(other) ((other) & 0x3)

#define ELF64_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xf))
#define ELF64_ST_BIND(info)       ((info) >> 4)
#define ELF64_ST_TYPE(info)       ((info) & 0xf)

/* ---- Special section indices ---- */
#define SHN_UNDEF  0
#define SHN_ABS    0xfff1
#define SHN_COMMON 0xfff2

/* ---- Program header types ---- */
#define PT_NULL       0
#define PT_LOAD       1
#define PT_DYNAMIC    2
#define PT_INTERP     3
#define PT_NOTE       4
#define PT_SHLIB      5
#define PT_PHDR       6
#define PT_TLS        7
#define PT_GNU_STACK  0x6474e551UL
#define PT_GNU_RELRO  0x6474e552UL

/* ---- Program header flags ---- */
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

/* ---- AArch64 relocations ---- */
#define R_AARCH64_NONE                0
#define R_AARCH64_ABS64               257
#define R_AARCH64_ABS32               258
#define R_AARCH64_PREL32              261
#define R_AARCH64_PREL16              262
#define R_AARCH64_MOVW_UABS_G0       263
#define R_AARCH64_MOVW_UABS_G0_NC    264
#define R_AARCH64_MOVW_UABS_G1       265
#define R_AARCH64_MOVW_UABS_G1_NC    266
#define R_AARCH64_MOVW_UABS_G2       267
#define R_AARCH64_MOVW_UABS_G2_NC    268
#define R_AARCH64_MOVW_UABS_G3       270
#define R_AARCH64_ADR_PREL_LO21      274
#define R_AARCH64_ADR_PREL_PG_HI21   275
#define R_AARCH64_ADD_ABS_LO12_NC    277
#define R_AARCH64_LDST8_ABS_LO12_NC  278
#define R_AARCH64_TSTBR14            279
#define R_AARCH64_CONDBR19           280
#define R_AARCH64_JUMP26             282
#define R_AARCH64_CALL26             283
#define R_AARCH64_LDST16_ABS_LO12_NC 284
#define R_AARCH64_LDST32_ABS_LO12_NC 285
#define R_AARCH64_LDST64_ABS_LO12_NC 286
#define R_AARCH64_LDST128_ABS_LO12_NC 299
#define R_AARCH64_ADR_GOT_PAGE        311
#define R_AARCH64_LD64_GOT_LO12_NC   312
#define R_AARCH64_GLOB_DAT            1025
#define R_AARCH64_JUMP_SLOT           1026
#define R_AARCH64_RELATIVE            1027

#define ELF64_R_SYM(info)  ((info) >> 32)
#define ELF64_R_TYPE(info) ((u32)(info))
#define ELF64_R_INFO(sym, type) (((u64)(sym) << 32) + (u32)(type))

/* ---- ELF64 header ---- */
typedef struct {
    u8  e_ident[EI_NIDENT];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} Elf64_Ehdr;

/* ---- Program header ---- */
typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} Elf64_Phdr;

/* ---- Section header ---- */
typedef struct {
    u32 sh_name;
    u32 sh_type;
    u64 sh_flags;
    u64 sh_addr;
    u64 sh_offset;
    u64 sh_size;
    u32 sh_link;
    u32 sh_info;
    u64 sh_addralign;
    u64 sh_entsize;
} Elf64_Shdr;

/* ---- Symbol table entry ---- */
typedef struct {
    u32 st_name;
    u8  st_info;
    u8  st_other;
    u16 st_shndx;
    u64 st_value;
    u64 st_size;
} Elf64_Sym;

/* ---- Relocation entry with addend ---- */
typedef struct {
    u64 r_offset;
    u64 r_info;
    i64 r_addend;
} Elf64_Rela;

/* ---- Dynamic section entry ---- */
typedef struct {
    i64 d_tag;
    union {
        u64 d_val;
        u64 d_ptr;
    } d_un;
} Elf64_Dyn;

/* ---- Dynamic tags ---- */
#define DT_NULL      0
#define DT_NEEDED    1
#define DT_HASH      4
#define DT_STRTAB    5
#define DT_SYMTAB    6
#define DT_RELA      7
#define DT_RELASZ    8
#define DT_RELAENT   9
#define DT_STRSZ     10
#define DT_SYMENT    11
#define DT_SONAME    14
#define DT_PLTGOT    3
#define DT_PLTRELSZ  2
#define DT_PLTREL    20
#define DT_JMPREL    23
#define DT_RELACOUNT 0x6ffffff9UL
#define DT_FLAGS     30
#define DT_FLAGS_1   0x6ffffffbUL
#define DT_BIND_NOW  24

/* DT_FLAGS values */
#define DF_BIND_NOW  0x8

/* DT_FLAGS_1 values */
#define DF_1_NOW     0x1
#define DF_1_PIE     0x8000000UL

/* DT_PLTREL value: use RELA relocations */
#define DT_RELA_VAL  7

/* ---- x86-64 machine type ---- */
#define EM_X86_64  62

/* ---- AR archive header ---- */
#define AR_MAGIC "!<arch>\n"
#define AR_MAGIC_LEN 8

typedef struct {
    char ar_name[16];
    char ar_date[12];
    char ar_uid[6];
    char ar_gid[6];
    char ar_mode[8];
    char ar_size[10];
    char ar_fmag[2];
} Ar_hdr;

#endif /* FREE_ELF_H */
