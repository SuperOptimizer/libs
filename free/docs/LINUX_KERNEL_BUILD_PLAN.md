# Linux Kernel Build Plan for the Free Toolchain

Definitive plan for compiling the entire Linux kernel for aarch64
using ONLY the free toolchain -- no gcc, no clang, no binutils.

Target: Linux 6.x kernel, defconfig for arm64, both vmlinux and modules.

---

## Table of Contents

1. [Kbuild Tool Requirements](#1-kbuild-tool-requirements)
2. [free-cc (C Compiler)](#2-free-cc-c-compiler)
3. [free-as (Assembler)](#3-free-as-assembler)
4. [free-ld (Linker)](#4-free-ld-linker)
5. [free-ar (Archiver)](#5-free-ar-archiver)
6. [free-objcopy](#6-free-objcopy)
7. [free-nm](#7-free-nm)
8. [free-strip](#8-free-strip)
9. [free-size](#9-free-size)
10. [free-readelf](#10-free-readelf)
11. [free-objdump (Existing)](#11-free-objdump-existing)
12. [Linker Script Support](#12-linker-script-support)
13. [Kernel Special Sections](#13-kernel-special-sections)
14. [GCC Builtins Required](#14-gcc-builtins-required)
15. [GCC Attributes Required](#15-gcc-attributes-required)
16. [GNU C Extensions Required](#16-gnu-c-extensions-required)
17. [Inline Assembly Support](#17-inline-assembly-support)
18. [Kernel Module (.ko) Support](#18-kernel-module-ko-support)
19. [KASLR Support](#19-kaslr-support)
20. [Phased Implementation Roadmap](#20-phased-implementation-roadmap)

---

## 1. Kbuild Tool Requirements

### 1.1 Tools Kbuild Invokes

The kernel build system (Kbuild) invokes the following tools, all configurable
via make variables. The free toolchain must replace every one:

| Make Variable | Default (GCC)              | Free Replacement | Status        |
|---------------|----------------------------|-------------------|---------------|
| CC            | $(CROSS_COMPILE)gcc        | free-cc           | Partial       |
| AS            | $(CROSS_COMPILE)as         | free-as           | Partial       |
| LD            | $(CROSS_COMPILE)ld         | free-ld           | Partial       |
| AR            | $(CROSS_COMPILE)ar         | free-ar           | Partial       |
| NM            | $(CROSS_COMPILE)nm         | free-nm           | Not started   |
| STRIP         | $(CROSS_COMPILE)strip      | free-strip        | Not started   |
| OBJCOPY       | $(CROSS_COMPILE)objcopy    | free-objcopy      | Not started   |
| OBJDUMP       | $(CROSS_COMPILE)objdump    | free-objdump      | Partial       |
| READELF       | $(CROSS_COMPILE)readelf    | free-readelf      | Not started   |
| SIZE          | $(CROSS_COMPILE)size       | free-size         | Not started   |
| HOSTCC        | gcc                        | free-cc           | Partial       |
| HOSTCXX       | g++                        | N/A (no C++)      | Not needed    |
| CPP           | $(CC) -E                   | free-cc -E        | Not started   |

### 1.2 Invocation Command

```
make ARCH=arm64 \
    CC=free-cc AS=free-as LD=free-ld AR=free-ar \
    NM=free-nm STRIP=free-strip OBJCOPY=free-objcopy \
    OBJDUMP=free-objdump READELF=free-readelf \
    HOSTCC=free-cc
```

### 1.3 Kbuild Environment Variables

Kbuild checks and uses these variables:

- KBUILD_CFLAGS: Compiler flags for target code (owned by top Makefile)
- KBUILD_AFLAGS: Assembler flags
- KBUILD_LDFLAGS: Linker flags
- KBUILD_CPPFLAGS: Preprocessor flags (includes -nostdinc)
- NOSTDINC_FLAGS: Contains -nostdinc to suppress system headers
- CROSS_COMPILE: Prefix for cross-compilation tools
- ARCH: Target architecture (arm64)
- KBUILD_VERBOSE: Build verbosity
- KBUILD_OUTPUT / O=: Out-of-tree build directory

### 1.4 Kbuild Probing

Kbuild uses `$(CC)` to probe compiler capabilities at configure time:

- `cc-option`: Test if CC accepts a flag (runs CC with flag, checks exit code)
- `cc-option-yn`: Same, returns y/n
- `cc-option-align`: Test alignment flag syntax
- `cc-disable-warning`: Test if -Wno-xxx is supported
- `cc-version`: Extract compiler version
- `cc-ifversion`: Conditional on compiler version
- `cc-name`: Detect compiler name (gcc, clang)

**Requirement**: free-cc must return 0 for recognized flags and nonzero for
unrecognized flags. It must also respond to `-dumpversion`, `--version`, and
`-print-file-name=` queries that Kbuild uses.

---

## 2. free-cc (C Compiler)

### 2.1 Current State

The free-cc compiler currently supports:
- C89/C99/C11/C23 language levels with feature flags
- GNU standard levels (gnu89/gnu99/gnu11/gnu23)
- Lexer, preprocessor, parser, AST, codegen for aarch64
- Optimization levels -O0 to -O3 with SSA IR at -O2+
- PIC code generation (-fPIC/-fpic)
- DWARF debug info (-g)
- GCC attribute parsing (section, weak, packed, aligned, visibility, etc.)
- GCC inline assembly (basic and extended, with operand substitution)
- GCC builtins (clz, ctz, popcount, ffs, bswap, atomics, va_args)
- Peephole optimizer
- x86_64 backend (stub)
- LTO infrastructure (IR serialization)

Lines of code: ~10,000 across cc/, not counting shared headers.

### 2.2 Flags the Kernel Requires

#### 2.2.1 Top-Level Makefile Flags (KBUILD_CFLAGS)

| Flag | Purpose | Status |
|------|---------|--------|
| -std=gnu11 | GNU C11 standard | HAVE (gnu11 level exists) |
| -nostdinc | No standard include dirs | NEED (not implemented) |
| -isystem DIR | System include path | NEED |
| -include FILE | Force-include a file | NEED |
| -Wall | All warnings | Partial (accepted, not all implemented) |
| -Wundef | Warn on undefined macros in #if | NEED |
| -Wstrict-prototypes | Require full prototypes | NEED |
| -Wno-trigraphs | Disable trigraph warnings | NEED |
| -fno-strict-aliasing | Disable type-based alias analysis | NEED (accept flag) |
| -fno-common | No common symbols (default) | NEED |
| -Werror-implicit-function-declaration | Error on implicit decls | NEED |
| -Wno-format-security | Disable format security warnings | NEED |
| -fno-delete-null-pointer-checks | Keep NULL checks | NEED (accept flag) |
| -fno-PIE | Disable PIE | NEED (flag acceptance) |
| -fno-stack-protector | No stack canaries | NEED (accept flag) |
| -fno-allow-store-data-races | Prevent store tearing | NEED (accept flag) |
| -fstack-protector-strong | Stack protection for kernel | NEED (optional, config-dep) |
| -O2 | Optimization level | HAVE |
| -Wframe-larger-than=N | Warn on large stack frames | NEED |
| -Wno-main | No warning about main() signature | NEED |
| -fno-asynchronous-unwind-tables | No .eh_frame | NEED |
| -fno-unwind-tables | No unwind tables | NEED |
| -Werror | Treat warnings as errors | NEED |
| -fmacro-prefix-map=X=Y | Remap macro paths | NEED |
| -ffunction-sections | Each function in own section | NEED |
| -fdata-sections | Each data item in own section | NEED |

#### 2.2.2 arch/arm64/Makefile Flags

| Flag | Purpose | Status |
|------|---------|--------|
| -mgeneral-regs-only | No FP/SIMD in kernel code | NEED (critical) |
| -mabi=lp64 | LP64 ABI | NEED (accept flag, default behavior) |
| -mlittle-endian | Little-endian target | NEED (accept, default) |
| -ffixed-x18 | Reserve x18 (shadow call stack) | NEED |
| -DKASAN_SHADOW_SCALE_SHIFT=N | KASAN define | HAVE (-D works) |
| -fno-omit-frame-pointer | Keep frame pointer | NEED |
| -march=armv8-a | Base ISA | NEED |
| -march=armv8.N-a+FEATURES | ISA extensions | NEED |
| -mbranch-protection=pac-ret+leaf | PAC return addresses | NEED (advanced) |
| -msign-return-address=all | Sign all return addrs | NEED (advanced) |
| -falign-functions=N | Function alignment | NEED |

#### 2.2.3 Preprocessor Flags

| Flag | Purpose | Status |
|------|---------|--------|
| -E | Preprocess only | NEED |
| -D NAME=VALUE | Define macro | HAVE |
| -U NAME | Undefine macro | NEED |
| -I DIR | Include path | HAVE |
| -isystem DIR | System include path | NEED |
| -include FILE | Force include | NEED |
| -nostdinc | No system includes | NEED |
| -Wp,-MMD,FILE | Dependency output | NEED (critical for Kbuild) |
| -Wp,-MT,TARGET | Dependency target | NEED |
| -M / -MM / -MD / -MMD | Dependency generation | NEED (critical) |

#### 2.2.4 Other Required Flags

| Flag | Purpose | Status |
|------|---------|--------|
| -c | Compile to .o only | HAVE |
| -S | Compile to .s only | HAVE |
| -o FILE | Output file | HAVE |
| -pipe | Use pipes | NEED (accept, can ignore) |
| -x assembler-with-cpp | Treat as assembly with preprocessing | NEED |
| -dumpversion | Print version | NEED |
| --version | Print version | NEED |
| -print-file-name=FILE | Find compiler file | NEED |
| -Wl,FLAG | Pass flag to linker | NEED |
| -w | Suppress all warnings | NEED |
| -P | Preprocessor: no linemarkers | NEED |
| -traditional-cpp / -no-integrated-as | Compatibility | Accept and ignore |

### 2.3 Implementation Plan for free-cc

#### Phase CC-1: Flag Acceptance (1-2 weeks)

Many flags do not change behavior -- the kernel build just needs the compiler
to accept them without erroring. Modify parse_args() in cc.c to accept all
flags from sections 2.2.1-2.2.4 above. For flags that modify behavior:

- -nostdinc: Set a flag to skip default include paths
- -isystem: Add to include paths with lower priority than -I
- -include: Prepend file as if #include at top of source
- -mgeneral-regs-only: Set flag to error on FP/NEON usage in codegen
- -ffixed-x18: Mark x18 as reserved in register allocator
- -ffunction-sections / -fdata-sections: Emit .text.funcname sections
- -fno-common: (Should already be default) No COMMON symbols in .o
- -fno-omit-frame-pointer: Force frame pointer usage

#### Phase CC-2: Dependency Generation (-MD/-MMD) (1 week)

Kbuild absolutely requires dependency file (.d file) generation.
The compiler must write a Makefile-format dependency file listing all
headers included during compilation.

Implementation:
1. Track all files opened by #include in pp.c
2. After preprocessing, write a .d file: `target.o: source.c header1.h header2.h ...`
3. Support -MMD (user headers only), -MD (all headers), -MT (target name)
4. Support -Wp,-MMD,file and -Wp,-MT,target passthrough

#### Phase CC-3: Preprocessor-Only Mode (-E) (1 week)

Add -E flag to emit preprocessed output to stdout.
Add -P to suppress #line markers.
Add -x assembler-with-cpp to preprocess .S assembly files.

This is needed because Kbuild preprocesses linker scripts (.lds.S -> .lds)
and assembly files (.S -> .s) using `$(CC) -E`.

#### Phase CC-4: -mgeneral-regs-only (1-2 weeks)

The kernel requires this on arm64 because the kernel does not save/restore
NEON/FP state across context switches for kernel code. The compiler must:

1. Never emit FP/NEON instructions for normal computation
2. Error if code uses float/double types (except in explicitly allowed sections)
3. Only use x0-x30, sp, xzr general purpose registers
4. The builtin_emit for popcount currently uses NEON (cnt v0.8b) --
   must provide an alternative using scalar bit manipulation

#### Phase CC-5: -ffixed-x18 (1 week)

Mark x18 as unavailable in the register allocator. The kernel reserves x18
for the shadow call stack. The register allocator in regalloc.c must be
modified to exclude x18 from the general-purpose register pool.

#### Phase CC-6: -ffunction-sections / -fdata-sections (1-2 weeks)

Instead of emitting all code into .text, emit each function into
.text.function_name and each global variable into .data.variable_name.
This enables --gc-sections in the linker to remove unused code.

The assembler directives emitted by gen.c must change from:
```
    .text
function:
```
to:
```
    .section .text.function,"ax",%progbits
function:
```

#### Phase CC-7: Predefined Macros (1 week)

The kernel checks for and requires these predefined macros:

| Macro | Value | Purpose |
|-------|-------|---------|
| __GNUC__ | 5 (or higher) | GCC version major |
| __GNUC_MINOR__ | 1 | GCC version minor |
| __GNUC_PATCHLEVEL__ | 0 | GCC version patch |
| __aarch64__ | 1 | Target architecture |
| __ARM_ARCH | 8 | ARM architecture version |
| __linux__ | 1 | Linux target |
| __unix__ | 1 | Unix target |
| __ELF__ | 1 | ELF format |
| __LP64__ | 1 | 64-bit longs/pointers |
| __BYTE_ORDER__ | __ORDER_LITTLE_ENDIAN__ | Endianness |
| __ORDER_LITTLE_ENDIAN__ | 1234 | LE marker |
| __ORDER_BIG_ENDIAN__ | 4321 | BE marker |
| __SIZEOF_LONG__ | 8 | sizeof(long) |
| __SIZEOF_POINTER__ | 8 | sizeof(void*) |
| __SIZEOF_INT__ | 4 | sizeof(int) |
| __SIZEOF_SHORT__ | 2 | sizeof(short) |
| __SIZEOF_LONG_LONG__ | 8 | sizeof(long long) |
| __CHAR_BIT__ | 8 | bits per char |
| __INT_MAX__ | 2147483647 | INT_MAX |
| __LONG_MAX__ | 9223372036854775807L | LONG_MAX |
| __LONG_LONG_MAX__ | 9223372036854775807LL | LLONG_MAX |
| __INTPTR_TYPE__ | long | intptr_t underlying type |
| __UINTPTR_TYPE__ | unsigned long | uintptr_t |
| __SIZE_TYPE__ | unsigned long | size_t |
| __PTRDIFF_TYPE__ | long | ptrdiff_t |
| __WCHAR_TYPE__ | unsigned int | wchar_t |
| __CHAR_UNSIGNED__ | (not defined or 0) | char signedness |
| __GCC_ASM_FLAG_OUTPUTS__ | 1 | asm flag outputs (v6.1+) |
| __GCC_HAVE_SYNC_COMPARE_AND_SWAP_1 | 1 | Atomic CAS support |
| __GCC_HAVE_SYNC_COMPARE_AND_SWAP_2 | 1 | |
| __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4 | 1 | |
| __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 | 1 | |

Many of these are used by the kernel's compiler_types.h and compiler-gcc.h.

#### Phase CC-8: Version and Probing Support (1 week)

Implement responses to compiler queries:

- `free-cc -dumpversion` -> print "14.0.0" (or similar GCC-compatible version)
- `free-cc --version` -> print version string
- `free-cc -print-file-name=include` -> print path to compiler include dir
- `free-cc -Werror=FLAG -c -x c /dev/null -o /dev/null` -> accept/reject test
- Return 0 for recognized flags, nonzero for unrecognized

---

## 3. free-as (Assembler)

### 3.1 Current State

The free-as assembler currently supports:
- Two-pass assembly (label collection then encoding)
- AArch64 instruction encoding via aarch64.h
- Sections: .text, .data, .rodata, .bss, .debug_*
- Directives: .global/.globl, .section, .byte, .hword, .word, .quad,
  .ascii, .asciz/.string, .align/.p2align, .zero/.space, .type, .size
- Relocations: R_AARCH64_CALL26, JUMP26, ADR_PREL_PG_HI21,
  ADD_ABS_LO12_NC, ABS64, ABS32, LDST8_ABS_LO12_NC, LDST64_ABS_LO12_NC,
  MOVW_UABS_G0/G1/G2/G3, ADR_GOT_PAGE, LD64_GOT_LO12_NC
- Symbol table (STT_FUNC, STT_NOTYPE, STB_GLOBAL)
- String literal unescaping
- DWARF debug sections

### 3.2 Missing Directives for Kernel

The kernel's assembly files (.S) use many directives not yet supported:

| Directive | Purpose | Status |
|-----------|---------|--------|
| .macro / .endm | Assembly macros | NEED (critical) |
| .if / .ifdef / .ifndef / .else / .endif | Conditional assembly | NEED (critical) |
| .set / .equ | Symbol aliases and constants | NEED |
| .weak | Weak symbol binding | NEED |
| .local | Local symbol binding | NEED |
| .hidden | Symbol visibility | NEED |
| .protected | Symbol visibility | NEED |
| .comm / .lcomm | Common/local common blocks | NEED |
| .fill | Fill with pattern | NEED |
| .incbin | Include binary file | NEED |
| .balign | Byte alignment | NEED |
| .pushsection / .popsection | Section stack | NEED (critical) |
| .previous | Return to previous section | NEED |
| .subsection | Subsection ordering | NEED |
| .ltorg / .pool | Literal pool | NEED |
| .req / .unreq | Register aliases | NEED (common in kernel) |
| .inst | Emit raw instruction word | NEED |
| .cfi_startproc / .cfi_endproc | Call frame info | NEED |
| .cfi_def_cfa / .cfi_offset / etc. | CFI directives | NEED |
| .loc | DWARF line info | Partial |
| .file | DWARF file info | NEED |
| .4byte / .8byte | Data directives (aliases) | NEED |
| .long | Alias for .word (4 bytes on arm64) | NEED |
| .purgem | Delete macro definition | NEED |
| .irp / .irpc | Repeat blocks | NEED |
| .rept / .endr | Repeat N times | NEED |
| .error / .warning | Emit diagnostic | NEED |
| .arch | Set architecture version | NEED (accept) |
| .arch_extension | Enable ISA extension | NEED (accept) |
| .cpu | Set CPU model | Accept and ignore |

### 3.3 Missing Instructions for Kernel

The kernel's arm64 assembly uses many instructions beyond basic arithmetic:

| Category | Instructions | Status |
|----------|-------------|--------|
| Exclusive loads/stores | LDXR, STXR, LDAXR, STLXR, LDXP, STXP | Partial (inline asm codegen uses them, assembler may not encode all) |
| Barriers | DMB, DSB, ISB, TLBI, DC, IC | NEED |
| System registers | MRS, MSR | NEED (critical) |
| Exception | SVC, HVC, SMC, ERET, BRK | Partial (SVC, BRK exist) |
| Cache | DC CVAC, DC CIVAC, IC IALLU, IC IALLUIS | NEED |
| TLB | TLBI VMALLE1, TLBI ASIDE1, etc. | NEED |
| Address generation | ADR (non-page) | HAVE |
| Conditional select | CSEL, CSINC, CSINV, CSNEG | NEED |
| Bit manipulation | UBFM, SBFM, BFM, UBFX, SBFX, BFI, BFXIL | NEED |
| Bit field | EXTR, RBIT, REV, REV16, REV32, CLZ, CLS | Partial |
| Multiply | SMULL, UMULL, SMULH, UMULH, MADD, MSUB, SMADDL, UMADDL | NEED |
| Division | UDIV | NEED (have SDIV) |
| Floating point | FMOV (int<->fp), FCMP, FADD, FSUB, FMUL, FDIV | Partial |
| NEON/SIMD | CNT, ADDV, LD1, ST1, etc. | Partial (for builtins) |
| Compare and branch | TBNZ, TBZ, CBZ, CBNZ | Partial |
| PC-relative | ADRP | HAVE |
| Atomics (v8.1) | LDADD, LDCLR, LDSET, LDEOR, SWP, CAS | NEED |
| Hints | NOP, YIELD, WFE, WFI, SEV, SEVL, PSB CSYNC | Partial (NOP) |
| BTI | BTI c/j/jc | NEED (branch target identification) |
| PAC | PACIA, PACIASP, AUTIA, AUTIASP, PACIB, AUTIB | NEED (pointer auth) |
| Memory tagging | (MTE instructions) | Low priority |
| Prefetch | PRFM | NEED |
| Conditional compare | CCMP, CCMN | NEED |
| Loads/stores | LDRSW, LDRSH, LDRSB (sign-extending) | NEED |
| Loads/stores | LDR/STR with register offset | NEED |
| Loads/stores | LDRH, STRH (16-bit) | NEED |
| Pair loads/stores | LDP, STP (pre/post-index) | Partial |
| Unaligned | LDUR, STUR | NEED |

### 3.4 Missing Relocation Types

The kernel module loader handles these relocation types, so the assembler
and linker must produce them:

| Relocation | Value | Purpose | Status |
|------------|-------|---------|--------|
| R_AARCH64_NONE | 0 | No relocation | HAVE |
| R_AARCH64_ABS64 | 257 | 64-bit absolute | HAVE |
| R_AARCH64_ABS32 | 258 | 32-bit absolute | HAVE |
| R_AARCH64_ABS16 | 259 | 16-bit absolute | NEED |
| R_AARCH64_PREL64 | 260 | 64-bit PC-relative | NEED |
| R_AARCH64_PREL32 | 261 | 32-bit PC-relative | NEED |
| R_AARCH64_PREL16 | 262 | 16-bit PC-relative | NEED |
| R_AARCH64_MOVW_UABS_G0 | 263 | MOVZ/MOVK immediate | NEED |
| R_AARCH64_MOVW_UABS_G0_NC | 264 | (no overflow check) | HAVE |
| R_AARCH64_MOVW_UABS_G1_NC | 266 | | HAVE |
| R_AARCH64_MOVW_UABS_G2_NC | 268 | | HAVE |
| R_AARCH64_MOVW_UABS_G3 | 270 | | HAVE |
| R_AARCH64_MOVW_SABS_G0 | 270 | Signed MOVN/MOVZ | NEED |
| R_AARCH64_MOVW_SABS_G1 | 271 | | NEED |
| R_AARCH64_MOVW_SABS_G2 | 272 | | NEED |
| R_AARCH64_ADR_PREL_PG_HI21 | 275 | ADRP page | HAVE |
| R_AARCH64_ADR_PREL_PG_HI21_NC | 276 | ADRP no check | NEED |
| R_AARCH64_ADD_ABS_LO12_NC | 277 | ADD low 12 | HAVE |
| R_AARCH64_LDST8_ABS_LO12_NC | 278 | LDR/STR byte | HAVE |
| R_AARCH64_TSTBR14 | 279 | TBZ/TBNZ 14-bit | NEED |
| R_AARCH64_CONDBR19 | 280 | B.cond 19-bit | NEED |
| R_AARCH64_JUMP26 | 282 | B 26-bit | HAVE |
| R_AARCH64_CALL26 | 283 | BL 26-bit | HAVE |
| R_AARCH64_LDST16_ABS_LO12_NC | 284 | LDR/STR halfword | NEED |
| R_AARCH64_LDST32_ABS_LO12_NC | 285 | LDR/STR word | NEED |
| R_AARCH64_LDST64_ABS_LO12_NC | 286 | LDR/STR doubleword | HAVE |
| R_AARCH64_LDST128_ABS_LO12_NC | 299 | LDR/STR quadword | NEED |
| R_AARCH64_ADR_GOT_PAGE | 311 | GOT page | HAVE |
| R_AARCH64_LD64_GOT_LO12_NC | 312 | GOT entry | HAVE |
| R_AARCH64_RELATIVE | 1027 | Dynamic relative | HAVE |

### 3.5 Arbitrary Section Support

Currently the assembler only supports fixed section IDs (SEC_TEXT through
SEC_DEBUG_STR, total 13). The kernel uses hundreds of custom sections:

- .init.text, .init.data, .init.rodata
- .exit.text, .exit.data
- .text.hot, .text.unlikely
- .data..percpu, .data..read_mostly
- .rodata.str1.1, .rodata.str1.8
- __ksymtab, __ksymtab_gpl, __ksymtab_strings
- __kcrctab, __kcrctab_gpl
- __ex_table
- .altinstructions, .altinstr_replacement
- .modinfo
- .note.GNU-stack
- .note.gnu.property
- And many more

**Requirement**: The assembler must support arbitrary section names with
arbitrary flags. The fixed SEC_TEXT/SEC_DATA/etc. scheme must be replaced
with a dynamic section table.

### 3.6 Implementation Plan for free-as

#### Phase AS-1: Dynamic Section Table (2-3 weeks)

Replace the fixed-size section arrays (MAX_CODE, MAX_DATA, etc.) with a
dynamic section table:

```
struct asm_section {
    char name[256];
    u32 type;        /* SHT_PROGBITS, SHT_NOBITS, etc. */
    u64 flags;       /* SHF_ALLOC | SHF_EXECINSTR | etc. */
    u8 *data;
    u64 size;
    u64 capacity;
    u64 alignment;
};
```

Parse .section directives to create new sections on the fly:
```
.section .init.text,"ax",%progbits
.section .exit.data,"aw",%progbits
.section __ex_table,"a",%progbits
```

Parse the flags string: "a" = SHF_ALLOC, "w" = SHF_WRITE, "x" = SHF_EXECINSTR,
"M" = SHF_MERGE, "S" = SHF_STRINGS, "G" = SHF_GROUP.
Parse the type: %progbits = SHT_PROGBITS, %nobits = SHT_NOBITS,
%note = SHT_NOTE, @progbits (AT&T syntax), etc.

#### Phase AS-2: Assembly Macros (.macro/.endm) (2 weeks)

Kernel .S files make extensive use of assembly macros. Implement:

```
.macro name arg1, arg2, ...
    body with \arg1 \arg2 substitution
.endm
```

- Store macro definitions in a table
- On invocation, expand arguments and re-lex the body
- Support default arguments: `.macro foo bar=0`
- Support .exitm for early exit
- Support .purgem to delete a macro
- Support recursive macros with expansion depth limit

#### Phase AS-3: Conditional Assembly (.if/.ifdef) (1 week)

```
.ifdef CONFIG_SMP
    dmb ish
.else
    nop
.endif
```

Support: .if EXPR, .ifdef SYMBOL, .ifndef SYMBOL, .else, .elseif,
.endif, .ifc STR1,STR2, .ifeq EXPR

#### Phase AS-4: Repeat Blocks (.rept/.irp) (1 week)

```
.rept 4
    nop
.endr

.irp reg, x0, x1, x2
    str \reg, [sp, #-8]!
.endr
```

#### Phase AS-5: Section Stack (.pushsection/.popsection) (1 week)

Kernel code constantly uses these to emit data into exception tables,
alternative instruction tables, etc. while in the middle of .text:

```
.text
function:
    ldr x0, [x1]
.pushsection __ex_table,"a"
    .long (1b - .), (fixup - .)
.popsection
    ret
```

Implement a section stack (push saves current section, pop restores).
Also implement .previous (swap to previous section).

#### Phase AS-6: Register Aliases (.req/.unreq) (1 week)

```
tmp .req x10
ptr .req x0
    mov tmp, #0
    ldr tmp, [ptr]
.unreq tmp
```

The lexer must resolve register aliases during tokenization.

#### Phase AS-7: Missing Instructions (3-4 weeks)

Add encoding support for all instructions in section 3.3.
Priority order:
1. MRS/MSR (system register access -- used everywhere in kernel)
2. DMB/DSB/ISB (barriers)
3. CSEL/CSINC/CSINV/CSNEG (conditional select)
4. Bit field instructions (UBFM, SBFM, BFI, UBFX)
5. Sign-extending loads (LDRSW, LDRSH, LDRSB)
6. TBNZ/TBZ (test-and-branch)
7. CCMP/CCMN (conditional compare)
8. Exclusive loads/stores (LDXR, STXR, etc.)
9. DC/IC/TLBI (cache/TLB maintenance)
10. PRFM (prefetch)
11. Atomics v8.1 (LDADD, SWP, CAS, etc.)
12. PAC instructions (PACIA, AUTIA, etc.)
13. BTI (branch target identification)

#### Phase AS-8: Missing Relocation Types (1-2 weeks)

Add support for emitting all relocation types in section 3.4.
The main additions are PREL32, PREL64, TSTBR14, CONDBR19,
LDST16/32/128_ABS_LO12_NC.

#### Phase AS-9: CFI Directives (2 weeks)

The kernel uses Call Frame Information (.cfi_*) directives extensively
for stack unwinding. Implement:

- .cfi_startproc / .cfi_endproc
- .cfi_def_cfa REG, OFFSET
- .cfi_def_cfa_offset OFFSET
- .cfi_def_cfa_register REG
- .cfi_offset REG, OFFSET
- .cfi_restore REG
- .cfi_adjust_cfa_offset OFFSET
- .cfi_remember_state / .cfi_restore_state

These generate .eh_frame sections with DWARF CFI bytecode.

#### Phase AS-10: .inst Directive and Expression Evaluation (1 week)

The kernel uses `.inst` to emit raw instruction words, often with
arithmetic expressions:

```
.inst 0xd503233f    /* PAC hint */
.inst 0xd50320ff | ((\imm) << 5)
```

The assembler needs expression evaluation in data-emitting directives.

---

## 4. free-ld (Linker)

### 4.1 Current State

The free-ld linker currently supports:
- Reading ELF64 relocatable objects (.o)
- Reading static archives (.a) with on-demand member extraction
- Global symbol resolution (first-definition wins)
- Section merging by name (.text, .data, .rodata, .bss)
- Two-segment layout (RX for text, RW for data)
- AArch64 relocation application
- Entry point resolution (-e flag)
- Shared library output (-shared) with dynamic sections
- GOT, PLT (basic), RELA, dynsym, dynstr, hash
- LTO support (reading/writing .free_ir sections)
- Fixed base address (0x400000)

### 4.2 What the Kernel Requires

The kernel does NOT use the default linker invocation. Instead, it uses
a custom linker script (vmlinux.lds) that controls every aspect of
the output layout. The linker must support:

1. **Linker script parsing and execution** (see section 12)
2. **Partial linking (-r / --relocatable)**: Combine .o files into one .o
3. **--gc-sections**: Remove unreferenced sections
4. **-Map FILE**: Generate link map
5. **--build-id**: Generate .note.gnu.build-id section
6. **--no-undefined**: Error on undefined symbols
7. **--whole-archive / --no-whole-archive**: Force-include all archive members
8. **-z noexecstack**: Mark stack as non-executable
9. **-z max-page-size=N**: Set page size for alignment
10. **--strip-debug**: Remove debug sections from output
11. **-T / --script FILE**: Use linker script
12. **Multiple output types**: ET_EXEC (vmlinux), ET_REL (.ko modules)
13. **Section group support**: COMDAT groups
14. **Weak symbol handling**: Weak definitions
15. **Symbol versioning**: For module versioning
16. **SORT sections**: Ordered section placement

### 4.3 Linker Flags Used by Kbuild

| Flag | Purpose | Status |
|------|---------|--------|
| -o FILE | Output file | HAVE |
| -T FILE / --script FILE | Linker script | NEED (critical) |
| -r / --relocatable | Partial linking | NEED (for modules) |
| -e SYMBOL | Entry point | HAVE |
| -Map FILE | Link map | NEED |
| --build-id | Build ID note | NEED |
| --no-undefined | Error on undef syms | NEED |
| --whole-archive | Include all archive members | NEED |
| --no-whole-archive | Stop above | NEED |
| --gc-sections | Remove unused sections | NEED |
| --print-gc-sections | Print removed sections | Accept |
| --emit-relocs | Keep relocations in output | NEED (for modules) |
| -z noexecstack | Non-executable stack | NEED |
| -z max-page-size=N | Page size | NEED |
| -z now | Eager binding | Accept |
| --strip-debug | Remove debug | NEED |
| -EL | Little-endian | Accept |
| --no-warn-mismatch | Ignore arch mismatches | Accept |
| -shared | Shared library | HAVE |
| -static | Static link | Accept (default) |
| --orphan-handling=warn | Warn on orphan sections | NEED |
| --sort-section=alignment | Sort sections by alignment | NEED |
| -z norelro | No RELRO | Accept |
| --no-apply-dynamic-relocs | | Accept |
| -pie | Position independent executable | NEED (for KASLR) |
| --defsym SYMBOL=VALUE | Define symbol | NEED |

### 4.4 Implementation Plan for free-ld

#### Phase LD-1: Linker Script Parser (4-6 weeks)

This is the single largest piece of work for kernel compilation.
See section 12 for the complete linker script specification.

#### Phase LD-2: Partial Linking (-r) (2-3 weeks)

Kernel module .ko files are created by partially linking multiple .o files
into a single relocatable .o file. The linker must:

1. Merge sections from inputs
2. Adjust relocations to refer to merged section offsets
3. Merge symbol tables
4. Output ET_REL (not ET_EXEC)
5. Preserve all relocation entries (do not apply them)
6. Handle section groups correctly

#### Phase LD-3: --gc-sections (2 weeks)

Dead code elimination:
1. Mark the entry point and all symbols referenced by KEEP() as roots
2. Trace references from root sections via relocations
3. Remove all sections not reachable from roots
4. Works with -ffunction-sections / -fdata-sections

#### Phase LD-4: --whole-archive (1 week)

When linking static archives, --whole-archive forces inclusion of every
member, not just those satisfying undefined references. The kernel uses this
for built-in.a files.

#### Phase LD-5: Weak Symbol Support (1 week)

The kernel uses weak symbols extensively:
- __weak function definitions that can be overridden
- Linker must prefer strong definitions over weak ones
- Undefined weak symbols resolve to 0 (no error)

Current status: The linker handles STB_GLOBAL but may not handle STB_WEAK.

#### Phase LD-6: --emit-relocs (1-2 weeks)

The kernel module build (modpost) requires relocations to be preserved in
the output .ko file so the kernel's module loader can apply them at load time.

#### Phase LD-7: -pie (Position Independent Executable) (2 weeks)

KASLR requires the vmlinux to be linked as -pie, producing R_AARCH64_RELATIVE
relocations for all absolute references. See section 19.

#### Phase LD-8: --build-id, -Map, and Other Flags (1 week)

- --build-id: Generate a .note.gnu.build-id section with SHA1 hash
- -Map FILE: Write a human-readable map of all symbols and sections
- --strip-debug: Remove .debug_* sections from output
- --defsym SYMBOL=VALUE: Add a symbol with a given value to the output

---

## 5. free-ar (Archiver)

### 5.1 Current State

The free-ar tool supports:
- Create (rcs): Build archive with symbol table
- List (t): List archive members
- Extract (x): Extract members to files
- Extended names (// member) for long filenames
- Symbol table (/ member) with big-endian offsets
- ELF symbol scanning for global symbols

### 5.2 What the Kernel Requires

Kbuild uses `ar` extensively. Required operations:

| Operation | Purpose | Status |
|-----------|---------|--------|
| ar rcs lib.a obj.o | Create/replace with symtab | HAVE |
| ar rcsD lib.a obj.o | Deterministic mode | NEED |
| ar t lib.a | List members | HAVE |
| ar x lib.a | Extract all members | HAVE |
| ar x lib.a member.o | Extract specific member | NEED |
| ar d lib.a member.o | Delete member | NEED |
| ar q lib.a obj.o | Quick append | NEED |
| ranlib lib.a | Rebuild symbol table | NEED (alias for ar s) |

### 5.3 Implementation Plan for free-ar

#### Phase AR-1: Deterministic Mode (D flag) (1 day)

Set UID/GID to 0, timestamp to 0, mode to 100644. This is mostly
already done -- verify and add the 'D' flag parsing.

#### Phase AR-2: Specific Member Extract and Delete (1 week)

- `ar x lib.a foo.o` -- extract only foo.o
- `ar d lib.a foo.o` -- remove foo.o from archive
- `ar q lib.a foo.o` -- append without checking for duplicates

#### Phase AR-3: Thin Archives (T flag) (1 week)

Kbuild uses thin archives (ar rcsT) where the archive stores
pathnames to the .o files instead of embedding the file contents.
This is a significant feature for kernel builds where built-in.a
files can contain thousands of objects.

#### Phase AR-4: ranlib (1 day)

Create a `free-ranlib` that is just `free-ar s` -- regenerate
the symbol table of an existing archive.

---

## 6. free-objcopy

### 6.1 Purpose

objcopy is used in the kernel build for:
1. Extracting the binary Image from vmlinux:
   `objcopy -O binary -R .comment -S vmlinux arch/arm64/boot/Image`
2. Adding/removing sections from ELF files
3. Converting between formats
4. Stripping symbols from modules

### 6.2 Required Features

| Flag | Purpose | Status |
|------|---------|--------|
| -O binary | Output raw binary (memory dump) | NEED |
| -O elf64-littleaarch64 | Output ELF | NEED |
| -I binary | Input is raw binary | NEED |
| -I elf64-littleaarch64 | Input ELF | NEED |
| -R .SECTION | Remove section | NEED |
| -S / --strip-all | Strip all symbols | NEED |
| --strip-debug | Strip debug symbols only | NEED |
| -j .SECTION | Keep only specified section | NEED |
| -B aarch64 | Set arch for binary input | NEED |
| --add-section NAME=FILE | Add section from file | NEED |
| --set-section-flags NAME=FLAGS | Set section flags | NEED |
| --rename-section OLD=NEW | Rename a section | NEED |
| --gap-fill BYTE | Fill gaps between sections | NEED |
| --pad-to ADDR | Pad output to address | NEED |
| -K SYMBOL | Keep symbol when stripping | NEED |
| -N SYMBOL | Remove specific symbol | NEED |
| -w | Wildcard matching for symbols | NEED |
| --localize-symbol SYM | Make symbol local | NEED |
| --globalize-symbol SYM | Make symbol global | NEED |
| --weaken-symbol SYM | Make symbol weak | NEED |
| --only-keep-debug | Strip to debug-only file | NEED |

### 6.3 Implementation Plan

#### Phase OC-1: Core Implementation (2-3 weeks)

Create src/objcopy/objcopy.c:
1. Read ELF file (reuse elf.c from linker)
2. Modify section table (add/remove/rename sections)
3. Modify symbol table (strip/localize/globalize)
4. Write ELF file

#### Phase OC-2: Binary Output (-O binary) (1 week)

This is the most critical feature for kernel builds.
For `-O binary`, iterate through all ALLOC sections in address order
and write their contents as a flat binary, with zero-fills for gaps.

Algorithm:
1. Sort sections by virtual address
2. Find lowest and highest addresses among ALLOC sections
3. Write bytes from lowest to highest, filling gaps with zeros
4. Skip sections removed by -R

#### Phase OC-3: Binary Input (-I binary) (1 week)

Convert a raw binary file to an ELF object:
1. Create ELF header with specified architecture
2. Create a single .data section containing the binary
3. Create symbols: _binary_FILENAME_start, _binary_FILENAME_end,
   _binary_FILENAME_size
4. Sanitize filename (replace dots/slashes with underscores)

---

## 7. free-nm

### 7.1 Purpose

nm lists symbols from ELF files. The kernel build uses nm to:
1. Extract symbol addresses for kallsyms
2. Verify symbol presence after linking
3. Check for duplicate or missing symbols

### 7.2 Required Features

| Flag | Purpose | Status |
|------|---------|--------|
| (default) | List all symbols with addresses | NEED |
| -g | Only global (external) symbols | NEED |
| -u | Only undefined symbols | NEED |
| -n | Sort by address | NEED |
| -p | No sorting | NEED |
| -S | Print symbol sizes | NEED |
| -D | Dynamic symbols | NEED |
| -A / --print-file-name | Print filename | NEED |
| --defined-only | Only defined symbols | NEED |
| --no-sort | Same as -p | NEED |
| --size-sort | Sort by size | NEED |
| -B | BSD output format | NEED |
| -P | POSIX output format | NEED |

### 7.3 Symbol Type Characters

nm prints a character indicating the symbol type:
- T/t: text (code) section
- D/d: data section
- B/b: BSS section
- R/r: read-only data
- U: undefined
- W/w: weak symbol
- A: absolute
- C: common
- N: debug symbol

Uppercase = global, lowercase = local.

### 7.4 Implementation Plan

#### Phase NM-1: Core Implementation (1-2 weeks)

Create src/nm/nm.c:
1. Read ELF file (parse Ehdr, Shdrs, find .symtab, .strtab)
2. Iterate symbols, determine type character from st_shndx and section flags
3. Format output: ADDRESS TYPE NAME
4. Support sorting options (-n, -p, --size-sort)
5. Support filtering (-g, -u, --defined-only)

---

## 8. free-strip

### 8.1 Purpose

strip removes symbols and debug information from ELF files. The kernel
build uses strip on modules (.ko files) after installation.

### 8.2 Required Features

| Flag | Purpose | Status |
|------|---------|--------|
| (default) | Strip all non-essential symbols | NEED |
| --strip-debug | Remove only debug sections | NEED |
| --strip-unneeded | Remove non-essential symbols | NEED |
| -K SYMBOL | Keep specified symbol | NEED |
| -N SYMBOL | Remove specified symbol | NEED |
| --keep-symbol=SYM | Same as -K | NEED |
| -o FILE | Write to different file | NEED |
| -R .SECTION | Remove specified section | NEED |
| --strip-all | Remove all symbols | NEED |

### 8.3 Implementation Plan

#### Phase ST-1: Core Implementation (1-2 weeks)

Create src/strip/strip.c. Implementation strategy:
1. Read entire ELF file into memory
2. Create output with sections filtered per options
3. Rebuild section header table without removed sections
4. Rebuild symbol table without stripped symbols
5. Fix up all section indices in remaining headers
6. Write output (in-place modification or to -o file)

---

## 9. free-size

### 9.1 Purpose

size displays section sizes of ELF files. Kbuild uses it for kernel
size reporting.

### 9.2 Required Features

| Flag | Purpose | Status |
|------|---------|--------|
| (default) | Print text, data, bss sizes | NEED |
| -A / --format=sysv | SysV format (per-section) | NEED |
| -B / --format=berkeley | Berkeley format (default) | NEED |
| -t / --totals | Print totals | NEED |

### 9.3 Implementation Plan

#### Phase SZ-1: Core Implementation (3 days)

Create src/size/size.c:
1. Read ELF file, iterate section headers
2. Classify each section as text (ALLOC+EXEC), data (ALLOC+WRITE),
   or bss (ALLOC+NOBITS)
3. Sum sizes per category
4. Print in Berkeley or SysV format

---

## 10. free-readelf

### 10.1 Purpose

readelf displays detailed ELF file information. Kbuild uses it for
verification and for scripts that inspect kernel objects.

### 10.2 Required Features

| Flag | Purpose | Status |
|------|---------|--------|
| -h | ELF header | NEED |
| -S | Section headers | NEED |
| -l | Program headers | NEED |
| -s | Symbol table | NEED |
| -r | Relocations | NEED |
| -d | Dynamic section | NEED |
| -n | Notes | NEED |
| -e | All headers (= -h -l -S) | NEED |
| -a | All (= -h -l -S -s -r -d -n) | NEED |
| -W | Wide output (do not wrap) | NEED |
| -p .SECTION | Dump section as strings | NEED |
| -x .SECTION | Hex dump of section | NEED |

### 10.3 Implementation Plan

#### Phase RE-1: Core Implementation (2 weeks)

Create src/readelf/readelf.c. Much of this overlaps with free-objdump.
The output format must match GNU readelf closely, as kernel scripts
parse readelf output with grep/awk.

---

## 11. free-objdump (Existing)

### 11.1 Current State

free-objdump currently supports:
- -h: Section headers
- -t: Symbol table
- -d: Disassembly (basic aarch64 instruction set)
- -r: Relocations
- -s: Hex dump

### 11.2 What the Kernel Requires

The kernel build invokes objdump rarely during normal builds, but it is
used by debugging tools and some build scripts. The main gap is instruction
coverage in the disassembler -- it needs to handle all aarch64 instructions,
not just the basic set.

### 11.3 Implementation Plan

#### Phase OD-1: Instruction Coverage (2-3 weeks)

Extend the disassembler to decode all instructions from section 3.3.
This parallels the assembler instruction work.

---

## 12. Linker Script Support

This is the most critical and complex requirement for kernel compilation.
The kernel's vmlinux is built entirely using a custom linker script.

### 12.1 Linker Script Commands Used by the Kernel

The arm64 vmlinux.lds.S (after preprocessing) uses these commands:

| Command | Purpose | Status |
|---------|---------|--------|
| OUTPUT_ARCH(aarch64) | Set output architecture | NEED |
| ENTRY(_text) | Set entry point symbol | NEED |
| PHDRS { ... } | Define program headers | NEED |
| SECTIONS { ... } | Define output sections | NEED |
| MEMORY { ... } | Define memory regions | NEED (maybe, check) |
| ASSERT(expr, msg) | Build-time assertions | NEED |
| PROVIDE(sym = expr) | Define symbol if not defined | NEED |
| PROVIDE_HIDDEN(sym = expr) | Same with hidden visibility | NEED |
| EXTERN(sym) | Force symbol to be undefined | NEED |
| NOCROSSREFS(sec1, sec2, ...) | Error if cross-references | NEED |
| INSERT AFTER section | Insert into another script | Low priority |

### 12.2 Section Definition Syntax

Within SECTIONS { ... }, the kernel uses:

```
.text : {
    _stext = .;
    *(.text.hot .text.hot.*)
    *(.text .text.*)
    *(.text.unlikely .text.unlikely.*)
    _etext = .;
} :text

.init.text : ALIGN(PAGE_SIZE) {
    _sinittext = .;
    KEEP(*(.init.text .init.text.*))
    _einittext = .;
}
```

Elements used within section definitions:

| Element | Purpose | Status |
|---------|---------|--------|
| . (dot) | Current location counter | NEED |
| . = EXPR | Set location counter | NEED |
| . = ALIGN(N) | Align location counter | NEED |
| *(.section) | Include all matching input sections | NEED |
| *(.sec1 .sec2) | Include multiple patterns | NEED |
| filename(.section) | From specific file only | NEED |
| KEEP(*(.section)) | Do not gc-sections | NEED |
| SORT(*)(.section) | Sort by filename | NEED |
| SORT_BY_NAME(*) | Sort inputs by name | NEED |
| SORT_BY_ALIGNMENT(*) | Sort by alignment | NEED |
| EXCLUDE_FILE(file) | Exclude file | NEED |
| BYTE(expr) | Emit 1 byte | NEED |
| SHORT(expr) | Emit 2 bytes | NEED |
| LONG(expr) | Emit 4 bytes | NEED |
| QUAD(expr) | Emit 8 bytes | NEED |
| FILL(pattern) | Set fill pattern | NEED |
| symbol = expr; | Define symbol | NEED |
| PROVIDE(sym = expr) | Conditional symbol | NEED |
| HIDDEN(sym = expr) | Hidden symbol | NEED |
| :phdr | Assign to program header | NEED |
| AT(addr) | Set load address (LMA) | NEED |
| ALIGN(n) | Alignment function | NEED |
| ALIGNOF(.section) | Get section alignment | NEED |
| SIZEOF(.section) | Get section size | NEED |
| ADDR(.section) | Get section address | NEED |
| LOADADDR(.section) | Get load address | NEED |
| DEFINED(sym) | Test if symbol is defined | NEED |
| MAX(a,b) / MIN(a,b) | Arithmetic | NEED |
| ABSOLUTE(expr) | Make value absolute | NEED |
| DATA_SEGMENT_ALIGN(max,min) | Data segment alignment | NEED |
| DATA_SEGMENT_END(.) | End data segment | NEED |
| /DISCARD/ : { ... } | Discard matching sections | NEED |

### 12.3 Expression Operators

Linker script expressions support:
- Arithmetic: +, -, *, /, %
- Bitwise: &, |, ^, ~, <<, >>
- Comparison: ==, !=, <, >, <=, >=
- Logical: !, &&, ||
- Ternary: ? :
- Assignment: =, +=, -=, *=, /=, &=, |=
- Parentheses: ( )
- Numeric constants: decimal, 0x hex, 0 octal
- Suffixes: K (1024), M (1048576)
- Symbol references

### 12.4 Kernel-Specific Linker Script Patterns

The kernel's include/asm-generic/vmlinux.lds.h defines macros that expand
to complex linker script patterns:

```
/* Init text sections */
#define INIT_TEXT                    \
    *(.init.text .init.text.*)      \
    MEM_DISCARD(init.text*)

/* Exception table */
#define EXCEPTION_TABLE(align)      \
    . = ALIGN(align);               \
    __start___ex_table = .;          \
    KEEP(*(__ex_table))              \
    __stop___ex_table = .;

/* Symbol table for modules */
#define KSYMTAB                     \
    . = ALIGN(8);                    \
    __start___ksymtab = .;           \
    KEEP(*(SORT(___ksymtab+*)))     \
    __stop___ksymtab = .;
```

### 12.5 Linker Script Parser Design

```
linker_script_parse(text) -> LinkerScript
    lex() -> tokens
    parse_top_level() handles:
        OUTPUT_ARCH, ENTRY, PHDRS, SECTIONS, MEMORY,
        ASSERT, PROVIDE, EXTERN, NOCROSSREFS, assignments

    parse_sections() handles:
        output_section_name : [AT(lma)] [ALIGN(n)] {
            input_section_descriptions
        } [:phdr] [=fill]

    parse_input_desc() handles:
        *(.text .text.*)
        KEEP(...)
        SORT(...)
        EXCLUDE_FILE(...)

    parse_expr() handles:
        All arithmetic, bitwise, comparison, ternary operators
        Function calls: ALIGN, SIZEOF, ADDR, etc.
        Symbol references, numeric constants
```

### 12.6 Implementation Plan for Linker Scripts

#### Phase LS-1: Lexer (1 week)

Tokenize linker script into: identifiers, numbers, strings, operators,
punctuation, keywords (SECTIONS, ENTRY, PROVIDE, KEEP, etc.).

Handle C-style comments (/* */), line continuations.

#### Phase LS-2: Expression Parser (1 week)

Implement a recursive-descent expression parser with full operator
precedence. Support all built-in functions (ALIGN, SIZEOF, ADDR, etc.).
The expression evaluator needs a symbol table for forward references.

#### Phase LS-3: Section Description Parser (2 weeks)

Parse section definitions including:
- Wildcard matching for input section names
- KEEP(), SORT(), EXCLUDE_FILE()
- Multiple input patterns
- Fill patterns
- Output section attributes (type, AT, ALIGN)
- Program header assignment (:phdr)
- /DISCARD/ sections

#### Phase LS-4: Script Executor (3-4 weeks)

Execute the parsed linker script:
1. Process PHDRS to create program header templates
2. Walk SECTIONS in order
3. For each output section:
   a. Match input sections from all objects against wildcard patterns
   b. Sort matched sections per SORT directives
   c. Concatenate matched section data
   d. Apply alignment
   e. Update location counter
   f. Define symbols at current location counter
4. Process PROVIDE: define symbols only if not already defined
5. Evaluate and check ASSERT conditions
6. Assign sections to program headers

#### Phase LS-5: Wildcard Pattern Matching (1 week)

Implement glob-style matching for section names:
- * matches any string
- ? matches any single character
- [abc] matches character set
- Support for `*(.text .text.*)` multi-pattern syntax

#### Phase LS-6: SORT Variants (1 week)

Implement sorting of input sections:
- SORT_BY_NAME (alphabetical)
- SORT_BY_ALIGNMENT (largest first)
- SORT_BY_INIT_PRIORITY (for .init_array)
- Nested sorts: SORT_BY_NAME(SORT_BY_ALIGNMENT(*))

---

## 13. Kernel Special Sections

The kernel uses dozens of special sections. The toolchain must handle
them correctly end-to-end (compiler -> assembler -> linker).

### 13.1 Section List

| Section | Purpose | Generated By |
|---------|---------|-------------|
| .text | Kernel code | Compiler |
| .text.hot | Hot (frequently called) code | Compiler (-ffunction-sections) |
| .text.unlikely | Cold code | Compiler |
| .init.text | Init-only code (freed after boot) | __init attribute |
| .init.data | Init-only data | __initdata attribute |
| .init.rodata | Init-only read-only data | __initconst attribute |
| .exit.text | Module exit code | __exit attribute |
| .exit.data | Module exit data | __exitdata attribute |
| .rodata | Read-only data | Compiler |
| .rodata.str1.1 | Merged strings (align 1) | Compiler (-fmerge-constants) |
| .data | Writable data | Compiler |
| .data..read_mostly | Rarely-modified data | __read_mostly attribute |
| .data..percpu | Per-CPU data | DEFINE_PER_CPU |
| .bss | Zero-initialized data | Compiler |
| __ksymtab | Exported symbol table | EXPORT_SYMBOL |
| __ksymtab_gpl | GPL-only exported symbols | EXPORT_SYMBOL_GPL |
| __ksymtab_strings | Symbol name strings | EXPORT_SYMBOL |
| __kcrctab | CRC table for exported symbols | CONFIG_MODVERSIONS |
| __kcrctab_gpl | CRC table (GPL) | CONFIG_MODVERSIONS |
| __ex_table | Exception fixup table | _ASM_EXTABLE |
| .altinstructions | Alternative instruction descriptors | ALTERNATIVE |
| .altinstr_replacement | Alternative instruction code | ALTERNATIVE |
| .modinfo | Module information strings | MODULE_INFO |
| .note.GNU-stack | Stack executability marker | Compiler/Assembler |
| .note.gnu.build-id | Build ID | Linker |
| .note.gnu.property | GNU property note | Compiler |
| .init_array | Constructor function pointers | __attribute__((constructor)) |
| .fini_array | Destructor function pointers | __attribute__((destructor)) |
| .got | Global Offset Table | Linker |
| .got.plt | GOT for PLT | Linker |
| .plt | Procedure Linkage Table | Linker |
| .rela.dyn | Dynamic relocations | Linker |
| .rela.plt | PLT relocations | Linker |
| .dynamic | Dynamic linking info | Linker |
| .dynsym | Dynamic symbol table | Linker |
| .dynstr | Dynamic string table | Linker |
| .hash / .gnu.hash | Symbol hash table | Linker |
| .strtab | String table | Assembler/Linker |
| .symtab | Symbol table | Assembler/Linker |
| .shstrtab | Section name strings | Assembler/Linker |
| .comment | Version info | Compiler |
| .debug_info | DWARF debug info | Compiler (-g) |
| .debug_abbrev | DWARF abbreviations | Compiler (-g) |
| .debug_line | DWARF line numbers | Compiler (-g) |
| .debug_str | DWARF strings | Compiler (-g) |
| .debug_frame | DWARF call frame info | Compiler (-g) |
| .eh_frame | Exception handling frame info | Compiler |
| .eh_frame_hdr | EH frame header | Linker |
| .ARM.exidx | ARM exception index (32-bit) | N/A for aarch64 |
| __versions | Module version CRCs | modpost |
| .gnu.linkonce.this_module | Module structure | modpost |

### 13.2 Section Attribute Requirements

The compiler must emit correct section attributes:

```c
/* From kernel's include/linux/init.h */
#define __init     __section(".init.text") __cold
#define __initdata __section(".init.data")
#define __initconst __section(".init.rodata")
#define __exit     __section(".exit.text")
#define __exitdata __section(".exit.data")

/* From kernel's include/linux/cache.h */
#define __read_mostly __section(".data..read_mostly")

/* From kernel's include/linux/export.h */
/* EXPORT_SYMBOL emits entries in __ksymtab + __ksymtab_strings */
```

The compiler's __attribute__((section("name"))) must emit the correct
.section directive with appropriate flags (alloc, write, exec).

---

## 14. GCC Builtins Required

### 14.1 Currently Implemented in free-cc

| Builtin | Status |
|---------|--------|
| __builtin_expect(x, v) | HAVE |
| __builtin_unreachable() | HAVE |
| __builtin_trap() | HAVE |
| __builtin_constant_p(x) | HAVE (always returns 0) |
| __builtin_offsetof(T, m) | HAVE |
| __builtin_types_compatible_p(T1, T2) | HAVE |
| __builtin_choose_expr(c, e1, e2) | HAVE |
| __builtin_clz/clzl/clzll | HAVE |
| __builtin_ctz/ctzl/ctzll | HAVE |
| __builtin_popcount/l/ll | HAVE |
| __builtin_ffs/ffsl/ffsll | HAVE |
| __builtin_parity/l/ll | HAVE |
| __builtin_bswap16/32/64 | HAVE |
| __builtin_memcpy | HAVE (calls memcpy) |
| __builtin_memset | HAVE (calls memset) |
| __builtin_strlen | HAVE (calls strlen) |
| __builtin_strcmp | HAVE (calls strcmp) |
| __atomic_load_n | HAVE |
| __atomic_store_n | HAVE |
| __atomic_exchange_n | HAVE |
| __atomic_compare_exchange_n | HAVE |
| __atomic_add_fetch | HAVE |
| __atomic_sub_fetch | HAVE |
| __sync_synchronize | HAVE |
| __builtin_va_start/end/arg/copy | HAVE |

### 14.2 Missing Builtins Required by Kernel

| Builtin | Purpose | Priority |
|---------|---------|----------|
| __builtin_add_overflow(a,b,res) | Checked addition | HIGH |
| __builtin_sub_overflow(a,b,res) | Checked subtraction | HIGH |
| __builtin_mul_overflow(a,b,res) | Checked multiplication | HIGH |
| __builtin_sadd_overflow | Signed add overflow | MEDIUM |
| __builtin_uadd_overflow | Unsigned add overflow | MEDIUM |
| __builtin_ssub_overflow | Signed sub overflow | MEDIUM |
| __builtin_usub_overflow | Unsigned sub overflow | MEDIUM |
| __builtin_smul_overflow | Signed mul overflow | MEDIUM |
| __builtin_umul_overflow | Unsigned mul overflow | MEDIUM |
| __builtin_assume_aligned(p, n) | Alignment hint | MEDIUM |
| __builtin_object_size(p, type) | Object size (for fortify) | HIGH |
| __builtin_frame_address(n) | Get frame pointer | MEDIUM |
| __builtin_return_address(n) | Get return address | MEDIUM |
| __builtin_extract_return_addr(p) | Normalize return addr | LOW |
| __builtin_prefetch(addr, rw, loc) | Prefetch hint | LOW |
| __builtin_huge_val() | Positive infinity | LOW |
| __builtin_nan("") | NaN | LOW |
| __builtin_inf() | Infinity | LOW |
| __builtin_isnan(x) | NaN test | LOW |
| __builtin_isinf(x) | Infinity test | LOW |
| __builtin_isfinite(x) | Finite test | LOW |
| __builtin_LINE() | Current line number | MEDIUM |
| __builtin_FILE() | Current file name | MEDIUM |
| __builtin_FUNCTION() | Current function name | MEDIUM |
| __atomic_fetch_add | Fetch-and-add | HIGH |
| __atomic_fetch_sub | Fetch-and-sub | HIGH |
| __atomic_fetch_and | Fetch-and-and | HIGH |
| __atomic_fetch_or | Fetch-and-or | HIGH |
| __atomic_fetch_xor | Fetch-and-xor | HIGH |
| __atomic_test_and_set | Test and set | MEDIUM |
| __atomic_clear | Clear | MEDIUM |
| __atomic_thread_fence | Thread fence | MEDIUM |
| __atomic_signal_fence | Signal fence | MEDIUM |
| __sync_val_compare_and_swap | CAS (old API) | HIGH |
| __sync_bool_compare_and_swap | CAS boolean | HIGH |
| __sync_fetch_and_add | Atomic add (old API) | HIGH |
| __sync_fetch_and_sub | Atomic sub | HIGH |
| __sync_fetch_and_or | Atomic or | HIGH |
| __sync_fetch_and_and | Atomic and | HIGH |
| __sync_fetch_and_xor | Atomic xor | HIGH |
| __sync_add_and_fetch | Add and fetch | HIGH |
| __sync_sub_and_fetch | Sub and fetch | HIGH |
| __sync_lock_test_and_set | Exchange | MEDIUM |
| __sync_lock_release | Release | MEDIUM |

### 14.3 Implementation Plan for Builtins

#### Phase BI-1: Overflow Builtins (1-2 weeks)

The kernel's include/linux/overflow.h wraps these. Implementation:

```c
/* __builtin_add_overflow(a, b, res):
   returns 1 if a+b overflows the type of *res, storing result in *res */
```

For aarch64 codegen:
- Use ADDS/SUBS and check the overflow/carry flags
- For unsigned: check carry flag (C)
- For signed: check overflow flag (V)
- Use CSET to capture the flag

#### Phase BI-2: Atomic Fetch Operations (1 week)

Extend the existing atomic builtin support with fetch-and-* variants.
These return the old value (before the operation), unlike add_fetch
which returns the new value.

For aarch64: Same LDAXR/STLXR loop, but return the loaded value
instead of the computed value.

#### Phase BI-3: Object Size and Frame Builtins (1 week)

- __builtin_object_size: Return -1 (unknown) or compute if possible
- __builtin_frame_address(0): Return x29 (frame pointer)
- __builtin_return_address(0): Return x30 (link register)

---

## 15. GCC Attributes Required

### 15.1 Currently Implemented

| Attribute | Status |
|-----------|--------|
| noreturn | HAVE |
| unused | HAVE |
| used | HAVE |
| weak | HAVE |
| packed | HAVE |
| deprecated | HAVE |
| noinline | HAVE |
| always_inline | HAVE |
| cold | HAVE |
| hot | HAVE |
| pure | HAVE |
| const | HAVE |
| malloc | HAVE |
| warn_unused_result | HAVE |
| constructor | HAVE |
| destructor | HAVE |
| transparent_union | HAVE |
| aligned(N) | HAVE |
| section("name") | HAVE |
| alias("name") | HAVE |
| visibility("hidden") | HAVE |
| format(printf,N,M) | HAVE (parsed, not checked) |
| cleanup(func) | HAVE |
| vector_size(N) | HAVE |

### 15.2 Missing Attributes Required by Kernel

| Attribute | Purpose | Priority |
|-----------|---------|----------|
| __attribute__((noclone)) | Prevent cloning | MEDIUM (accept) |
| __attribute__((no_sanitize("..."))) | Disable sanitizer | LOW (accept) |
| __attribute__((assume_aligned(N))) | Alignment assumption | MEDIUM |
| __attribute__((alloc_size(N))) | Allocation size | MEDIUM (parsed) |
| __attribute__((error("msg"))) | Force compile error | HIGH |
| __attribute__((warning("msg"))) | Force compile warning | HIGH |
| __attribute__((externally_visible)) | Force external visibility | LOW |
| __attribute__((flatten)) | Inline all calls | LOW (accept) |
| __attribute__((no_instrument_function)) | Skip profiling | LOW (parsed) |
| __attribute__((returns_nonnull)) | Non-null return | LOW (parsed) |
| __attribute__((sentinel)) | NULL-terminated args | LOW (parsed) |
| __attribute__((designated_init)) | Require designated init | LOW |
| __attribute__((nonnull(N))) | Non-null parameter | LOW (parsed) |
| __attribute__((access(mode,N))) | Access mode annotation | LOW (parsed) |
| __attribute__((copy(func))) | Copy attributes | LOW |
| __attribute__((optimize("..."))) | Per-function optimization | MEDIUM |
| __attribute__((target("..."))) | Per-function target | MEDIUM |
| __attribute__((naked)) | No prologue/epilogue | HIGH |
| __attribute__((interrupt)) | Interrupt handler | MEDIUM |
| __attribute__((fallthrough)) | Switch fallthrough marker | HIGH |

### 15.3 Implementation Plan

Most of these can be parsed and ignored without affecting correctness.
Priority items:

- **error/warning**: Emit compile error/warning when function is called
- **naked**: Do not emit function prologue/epilogue (needed for some asm stubs)
- **fallthrough**: Suppress fallthrough warnings in switch (marker attribute)
- Other attributes: Parse and silently accept

---

## 16. GNU C Extensions Required

### 16.1 Currently Supported

The compiler supports gnu89/gnu99/gnu11/gnu23 standard levels and already
handles __extension__, __typeof__, __auto_type, __attribute__.

### 16.2 Missing Extensions Required by Kernel

| Extension | Purpose | Priority |
|-----------|---------|----------|
| Statement expressions ({ ... }) | Compute value in block | CRITICAL |
| typeof / __typeof__ | Get type of expression | HAVE (partial) |
| __auto_type | Auto type deduction | HAVE |
| Designated initializers (GNU style) | .field = value | HAVE |
| Case ranges (case 'a' ... 'z':) | Range in switch case | HIGH |
| Zero-length arrays | struct hack (pre-C99) | NEED |
| Variadic macros | __VA_ARGS__ | HAVE |
| Computed goto (&&label, goto *ptr) | Jump tables | HIGH |
| Local labels (__label__) | Labels in statement exprs | HIGH |
| Nested functions | Functions inside functions | LOW |
| __builtin_constant_p in initializers | Const-fold in init | HIGH |
| Binary constants (0b...) | Binary literals | HAVE |
| __attribute__ syntax | Attribute syntax | HAVE |
| asm labels (asm("name")) | Symbol name override | NEED |
| Transparent unions | Function arg unions | HAVE |
| Dollar signs in identifiers | $identifier | LOW |
| Empty structures | struct {} | NEED |
| Variable-length arrays in structs | Kernel avoids, but headers use | LOW |
| Conditionals with omitted operand (x ?: y) | Elvis operator | HIGH |
| __int128 | 128-bit integer type | MEDIUM |
| _Static_assert in older GNU modes | Backported feature | HAVE |
| __COUNTER__ macro | Unique counter | HIGH |
| __PRETTY_FUNCTION__ | Function name with signature | MEDIUM |

### 16.3 Implementation Plan

#### Phase GE-1: Statement Expressions (2-3 weeks)

This is the single most important missing GNU extension for the kernel.
Nearly every kernel macro uses statement expressions:

```c
#define min(a, b) ({       \
    typeof(a) _a = (a);    \
    typeof(b) _b = (b);    \
    _a < _b ? _a : _b;    \
})
```

Implementation:
1. Parse ({ ... }) as an expression
2. The value is the last expression in the block
3. Create a new AST node (ND_STMT_EXPR)
4. The block creates a new scope
5. Codegen: emit all statements, leave last expression's value in register

#### Phase GE-2: Case Ranges (1 week)

```c
case 'a' ... 'z':
```

Parse `...` between two constant expressions in case labels.
Expand to a range check in codegen.

#### Phase GE-3: Computed Goto (1-2 weeks)

```c
static void *table[] = { &&label1, &&label2, ... };
goto *table[index];
```

Implementation:
1. &&label takes the address of a label
2. goto *expr does an indirect branch
3. Codegen: `br xN` to the address

#### Phase GE-4: Asm Labels (1 week)

```c
extern int foo asm("bar");  /* symbol "bar" for variable "foo" */
```

The compiler must use the asm name as the actual symbol name in the
generated assembly output.

#### Phase GE-5: Elvis Operator (1 day)

```c
x ?: y  /* equivalent to x ? x : y, but evaluates x only once */
```

Parse `?:` as a special ternary where the middle operand is the condition.

#### Phase GE-6: __COUNTER__ Macro (1 day)

A preprocessor macro that expands to a unique integer, incrementing
with each expansion. Used in the kernel for generating unique identifiers.

---

## 17. Inline Assembly Support

### 17.1 Current State

The compiler supports:
- Basic asm (asm("instruction"))
- Extended asm with operands: asm("template" : outputs : inputs : clobbers)
- volatile and goto variants
- Symbolic operand names [name]
- Operand substitution (%0, %1, %[name])
- Register constraints: "r" (register), "m" (memory), "i" (immediate)
- Clobber parsing

### 17.2 Missing Features for Kernel

| Feature | Purpose | Priority |
|---------|---------|----------|
| Proper constraint handling | Correct register assignment | CRITICAL |
| "=r" output constraint | Write-only register | CRITICAL |
| "+r" read-write constraint | Read and write register | CRITICAL |
| "=&r" early-clobber | Output clobbered before inputs read | HIGH |
| "=@ccXX" flag outputs | Condition code outputs | HIGH |
| Specific register constraints ("=r" -> actual reg alloc) | | CRITICAL |
| Memory operand "m" | Address of variable | HIGH |
| Immediate constraints "i", "n" | Integer immediate | HAVE (partial) |
| "K" constraint | Logical immediate | MEDIUM |
| "Ush" constraint | Symbol reference | MEDIUM |
| Multi-alternative constraints | "r,m" choice | MEDIUM |
| Operand modifiers (%w0, %x0, %b0) | Register size override | HIGH |
| "cc" clobber | Condition codes | HAVE (ignore) |
| "memory" clobber | Memory barrier | HAVE (accept) |
| Goto labels (asm goto) | Branch to C label | HIGH |
| Tied operands ("0" constraint) | Same register as operand 0 | HIGH |
| Inline asm in constant expressions | Compile-time asm | LOW |

### 17.3 Operand Modifiers for AArch64

The kernel's inline assembly uses register size modifiers:

| Modifier | Meaning | Example |
|----------|---------|---------|
| %w0 | 32-bit register (w0-w30) | "add %w0, %w1, %w2" |
| %x0 | 64-bit register (x0-x30) | "add %x0, %x1, %x2" |
| %0 | Default size for type | |
| %H0 | High part of pair | For LDP/STP |
| %c0 | Bare constant (no #) | For immediates |

### 17.4 Implementation Plan

#### Phase IA-1: Proper Register Allocation for Inline Asm (3-4 weeks)

The current implementation assigns x0, x1, ... sequentially to operands.
This is wrong. The correct approach:

1. For each operand, parse the constraint to determine:
   - Which register class (GP, FP, specific register)
   - Whether it is input, output, or read-write
   - Early-clobber status

2. Allocate registers for operands, respecting constraints:
   - "r" = any GP register
   - Specific register constraints need exact register
   - "+r" needs same register for input and output
   - "=&r" must not overlap any input register

3. Generate loads/stores around the asm block:
   - Load input operands into allocated registers
   - After asm, store output operands back to their variables

4. Respect the clobber list:
   - Save/restore any callee-saved registers that are clobbered

#### Phase IA-2: Operand Modifiers (1 week)

Parse %w0, %x0, %c0, %H0 in templates and emit the appropriate
register name (w-reg vs x-reg) or constant format.

#### Phase IA-3: Asm Goto (1-2 weeks)

```c
asm goto("tbz %0, #0, %l[label]"
         : /* no outputs */
         : "r"(val)
         : /* no clobbers */
         : label);
```

The asm goto extension allows branching to C labels from inline asm.
Implementation requires:
1. Parse the goto label list (4th colon section)
2. Map %l[label] to the actual label in generated assembly
3. Ensure the compiler does not assume fallthrough after asm goto

---

## 18. Kernel Module (.ko) Support

### 18.1 Module File Format

Kernel modules (.ko) are ELF relocatable objects (ET_REL) with:
- Standard code/data sections
- .modinfo section with key=value strings
- .gnu.linkonce.this_module section
- __ksymtab / __ksymtab_gpl for exported symbols
- __kcrctab / __kcrctab_gpl for CRC checksums (CONFIG_MODVERSIONS)
- __versions section for import CRC verification
- All relocations preserved (not resolved)

### 18.2 Module Build Process

1. Compile .c files to .o (standard compilation)
2. Partial link with `ld -r` to create combined .o
3. Run `scripts/mod/modpost` to:
   - Check for undefined symbols
   - Generate .mod.c with module metadata
   - Check CRC version compatibility
4. Compile .mod.c to .mod.o
5. Final `ld -r` to combine .o + .mod.o into .ko
6. Optional: strip debug symbols

### 18.3 Requirements for Toolchain

| Requirement | Tool | Status |
|-------------|------|--------|
| Compile C with all kernel features | free-cc | Partial |
| Assemble .S files | free-as | Partial |
| Partial link (ld -r) | free-ld | NEED |
| Preserve relocations in output | free-ld | NEED |
| Generate .modinfo section | free-cc (via __attribute__((section))) | HAVE |
| Strip debug from .ko | free-strip | NEED |
| List symbols | free-nm | NEED |

### 18.4 CONFIG_MODVERSIONS

When enabled, every exported symbol gets a CRC computed from its prototype.
The CRC is stored in __kcrctab sections. When loading a module, the kernel
compares CRCs to verify ABI compatibility.

The free-cc compiler does not need to compute CRCs -- that is done by
the modpost tool (compiled with HOSTCC). However, free-cc must be able
to serve as HOSTCC to compile modpost itself.

---

## 19. KASLR Support

### 19.1 Overview

Kernel Address Space Layout Randomization on arm64 requires:
1. The vmlinux is linked as a position-independent executable (-pie)
2. All absolute relocations produce R_AARCH64_RELATIVE entries
3. The kernel self-relocates at boot using these entries

### 19.2 Linker Requirements for KASLR

| Requirement | Description |
|-------------|-------------|
| -pie output | Link as position-independent executable (ET_DYN with entry) |
| R_AARCH64_RELATIVE | Generate relative relocations for all absolute addresses |
| .rela.dyn section | Contains all runtime relocations |
| Split 64-bit fields | Some fields split into 32-bit halves for build-time resolution |
| PT_GNU_RELRO | (optional) Read-only after relocation |

### 19.3 Code Generation Requirements

The compiler already uses ADRP/ADD for symbol references, which are
PC-relative. For KASLR, this is actually sufficient for code references.
Data references to absolute addresses need R_AARCH64_RELATIVE relocations.

### 19.4 Implementation Plan

#### Phase KA-1: -pie Linking (2-3 weeks)

1. Support ET_DYN output type with an entry point
2. For every R_AARCH64_ABS64 relocation in the input, generate a
   R_AARCH64_RELATIVE entry in .rela.dyn
3. Create the minimal dynamic section needed for the kernel's
   relocation processing
4. Generate PT_DYNAMIC program header

---

## 20. Phased Implementation Roadmap

### Phase 0: Foundation (Current)
**Status: Done**
- Basic C89-C23 compiler
- Basic assembler
- Basic linker
- Archive tool
- Object dumper

### Phase 1: Flag Acceptance and Probing (2-3 weeks)
**Goal: Kbuild can detect and configure free-cc**

Tasks:
- CC-1: Accept all required flags (silently ignore non-behavioral ones)
- CC-7: Predefined macros (__GNUC__, __aarch64__, etc.)
- CC-8: Version and probing responses (-dumpversion, --version, etc.)

Milestone: `make ARCH=arm64 CC=free-cc defconfig` completes
without errors about unrecognized flags.

### Phase 2: Preprocessor Completeness (2-3 weeks)
**Goal: Can preprocess any kernel source file**

Tasks:
- CC-2: Dependency generation (-MD/-MMD/-MT)
- CC-3: Preprocessor-only mode (-E, -P)
- CC-3b: Preprocessor support for .S files (-x assembler-with-cpp)
- GE-6: __COUNTER__ macro

Milestone: `free-cc -E -nostdinc -I... kernel/sched/core.c` produces
valid preprocessed output with correct .d files.

### Phase 3: GNU C Extensions (4-6 weeks)
**Goal: Can parse all kernel C source files**

Tasks:
- GE-1: Statement expressions ({ ... }) -- CRITICAL
- GE-2: Case ranges
- GE-3: Computed goto
- GE-4: Asm labels
- GE-5: Elvis operator
- BI-1: Overflow builtins
- BI-2: Atomic fetch operations
- BI-3: Object size and frame builtins

Milestone: `free-cc -fsyntax-only` on major kernel files (kernel/sched/core.c,
mm/page_alloc.c, fs/read_write.c) parses without errors.

### Phase 4: Compiler Codegen for Kernel (4-6 weeks)
**Goal: Can compile simple kernel source files to correct assembly**

Tasks:
- CC-4: -mgeneral-regs-only
- CC-5: -ffixed-x18
- CC-6: -ffunction-sections / -fdata-sections
- IA-1: Proper register allocation for inline asm
- IA-2: Operand modifiers
- IA-3: Asm goto

Milestone: Can compile kernel/printk/printk.c and kernel/panic.c
to assembly that assembles correctly.

### Phase 5: Assembler Completeness (6-8 weeks)
**Goal: Can assemble all kernel .S and compiler-generated .s files**

Tasks:
- AS-1: Dynamic section table (arbitrary sections)
- AS-2: Assembly macros (.macro/.endm)
- AS-3: Conditional assembly (.if/.ifdef)
- AS-4: Repeat blocks (.rept/.irp)
- AS-5: Section stack (.pushsection/.popsection)
- AS-6: Register aliases (.req/.unreq)
- AS-7: Missing instructions (MRS, MSR, barriers, etc.)
- AS-8: Missing relocation types
- AS-9: CFI directives
- AS-10: .inst directive and expression evaluation

Milestone: All arch/arm64/kernel/*.S files assemble without errors.

### Phase 6: Binary Utilities (3-4 weeks)
**Goal: All binutils replacements exist**

Tasks:
- OC-1 through OC-3: free-objcopy
- NM-1: free-nm
- ST-1: free-strip
- SZ-1: free-size
- RE-1: free-readelf
- AR-1 through AR-4: free-ar improvements

Milestone: `free-objcopy -O binary -S vmlinux Image` works.
`free-nm vmlinux` lists all symbols correctly.

### Phase 7: Linker Script Engine (8-12 weeks)
**Goal: Can execute the kernel's vmlinux.lds linker script**

Tasks:
- LS-1: Linker script lexer
- LS-2: Expression parser
- LS-3: Section description parser
- LS-4: Script executor
- LS-5: Wildcard matching
- LS-6: SORT variants
- LD-2: Partial linking (-r)
- LD-3: --gc-sections
- LD-4: --whole-archive
- LD-5: Weak symbol support
- LD-6: --emit-relocs
- LD-7: -pie for KASLR
- LD-8: --build-id, -Map

Milestone: `free-ld -T vmlinux.lds *.o -o vmlinux` produces a valid
vmlinux that boots in QEMU.

### Phase 8: End-to-End Kernel Build (4-6 weeks)
**Goal: Full kernel compilation from make to bootable Image**

Tasks:
- Integration testing with actual kernel source
- Fix all remaining issues discovered during end-to-end build
- Handle edge cases in real kernel code
- Compile host tools (scripts/kallsyms, scripts/sorttable, etc.)
- Verify module (.ko) building works

Milestone: Complete `make ARCH=arm64 CC=free-cc ... defconfig all`
produces a bootable Image and functional modules.

### Phase 9: Verification and Hardening (2-4 weeks)
**Goal: Production-quality kernel builds**

Tasks:
- Compare output with GCC-built kernel (readelf, objdump analysis)
- Boot test in QEMU with initrd
- Load module test
- Compare kernel sizes
- Stress test with allyesconfig / allmodconfig
- Fix any remaining ABI compatibility issues

Milestone: Free-toolchain-built kernel passes Linux Test Project (LTP)
basic test suite.

---

## Estimated Total Timeline

| Phase | Duration | Cumulative |
|-------|----------|------------|
| Phase 1: Flag Acceptance | 2-3 weeks | 2-3 weeks |
| Phase 2: Preprocessor | 2-3 weeks | 4-6 weeks |
| Phase 3: GNU Extensions | 4-6 weeks | 8-12 weeks |
| Phase 4: Codegen | 4-6 weeks | 12-18 weeks |
| Phase 5: Assembler | 6-8 weeks | 18-26 weeks |
| Phase 6: Binutils | 3-4 weeks | 21-30 weeks |
| Phase 7: Linker Scripts | 8-12 weeks | 29-42 weeks |
| Phase 8: Integration | 4-6 weeks | 33-48 weeks |
| Phase 9: Verification | 2-4 weeks | 35-52 weeks |

**Total estimated effort: 9-12 months**, with the linker script engine
and GNU C extension support being the two largest pieces of work.

The phases are partially parallelizable:
- Phases 1-4 (compiler) can proceed in parallel with Phase 5 (assembler)
- Phase 6 (binutils) can proceed independently
- Phase 7 (linker) depends on Phase 5 completion
- Phase 8-9 depend on all preceding phases

---

## Key Risk Areas

1. **Statement expressions**: Used in virtually every kernel macro.
   Without this, nothing compiles. Must be the first priority after
   basic flag acceptance.

2. **Linker script engine**: The kernel's linker script is extremely
   complex. This is the largest single implementation effort.

3. **Inline assembly correctness**: The kernel's inline assembly is
   sophisticated, with precise register constraints. Incorrect register
   allocation causes silent data corruption.

4. **ABI compatibility**: The generated code must be ABI-compatible with
   GCC/Clang output. Calling convention, structure layout, and alignment
   must match exactly.

5. **Preprocessor edge cases**: The kernel preprocessor usage is extensive
   and tests many corner cases (macro recursion, stringification, token
   pasting, variadic macros with zero args).

6. **Scale**: The kernel builds ~20,000 .c files and ~500 .S files.
   The tools must handle this volume without memory exhaustion or
   excessive build times.
