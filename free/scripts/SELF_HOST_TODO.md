# Self-Hosting TODO for the free Toolchain

Status: **Stage 1 builds and works.** The compiler can produce correct AArch64
assembly for simple C programs. Self-compilation (stage 2) fails on several
language features the compiler's own source code uses.

## Current State

- Stage 1 (gcc builds free-cc): **WORKING** (after build fixes applied)
- Stage 2 (free-cc compiles itself): **BLOCKED** (see below)
- Stage 3 (stage-2 free-cc compiles itself): blocked until stage 2 works

## Build Infrastructure Done

- [x] `scripts/bootstrap.sh` -- 3-stage bootstrap script with verification
- [x] `src/cc/util.c` -- arena allocator, string utilities, error handling
  (these were previously only in test stubs)
- [x] `CMakeLists.txt` updated with util.c in free-cc sources
- [x] Compiler driver (cc.c) fixed: correct `parse()` API, include path
  registration via `pp_add_include_path`
- [x] Preprocessor fix: `pp_add_include_path` no longer segfaults when called
  before `pp_init` (store raw pointer instead of arena-allocated copy)
- [x] Parser fix: `void *` parameters in function prototypes (e.g.
  `void *memcpy(void *dst, ...)`) now parse correctly
- [x] Parser fix: function parameter names now stored in type->name
  so they're registered as local variables (was treating params as globals)
- [x] Parser fix: funcdef now builds arg nodes and sets fn->offset
  for code generator parameter spilling and frame size calculation
- [x] Parser fix: forward declaration of parse_type_name added

## Critical Blockers for Self-Hosting

### 1. `stdarg.h` / Variadic Functions (BLOCKER - HIGHEST PRIORITY)

The libc `stdarg.h` uses `__builtin_va_list` and `__builtin_va_start` etc.
The free compiler does not understand GCC builtins. Files affected:
`gen.c`, `util.c`, `cc.c`, `pp.c` (anything that uses `va_list`).

**Fix**: Provide an AArch64-specific `va_list` definition for self-hosting:
```c
/* AArch64 va_list is a struct: */
typedef struct {
    void *__stack;
    void *__gr_top;
    void *__vr_top;
    int   __gr_offs;
    int   __vr_offs;
} va_list;
```
Then implement `va_start`, `va_arg`, `va_end` as compiler intrinsics or
inline assembly in the code generator.

### 2. Preprocessor: `#include <...>` Segfault with Complex Headers

Including `free.h` (which has many `#define` macros) causes the parser to
fail with "expected ';'" errors. The preprocessor handles simple includes
fine but appears to have issues with complex headers containing many macros
or large enum/struct definitions spanning many tokens. The exact failure
point needs debugging -- likely an arena overflow or token buffer overflow
in the preprocessor when expanding large numbers of macro definitions.

**Fix**: Debug the preprocessor's handling of large headers. May need to
increase `PP_MAX_MACROS` (currently 4096) or fix token buffering.

### 3. Missing `lex()` and `preprocess()` Standalone APIs

The `cc.c` driver was updated to call `parse()` directly (which internally
runs lex + preprocess). However, the bootstrap script's compile_sources
function assumes separate compilation steps. The API is consistent now,
but needs verification that the whole pipeline works end-to-end for each
source file.

## Parser Features Needed (mostly implemented, needs testing)

These features ARE implemented in `parse.c` but need testing with the
compiler's own source:

- [x] `enum` declarations with explicit values
- [x] `struct` with pointer members
- [x] `union` types
- [x] `typedef` (including typedef of struct pointers)
- [x] `switch`/`case`/`default`/`break`
- [x] `for`/`while`/`do-while`/`continue`
- [x] `goto`/labels
- [x] Pointer arithmetic
- [x] Array subscript (`a[i]`)
- [x] Member access (`.` and `->`)
- [x] `sizeof` on types and expressions
- [x] Cast expressions
- [x] Ternary operator (`? :`)
- [x] Compound assignment (`+=`, `-=`, etc.)
- [x] Pre/post increment/decrement
- [x] String literals with escape sequences
- [x] Global variables

## Parser Features Possibly Missing

- [ ] Function pointers as struct members (common in the compiler:
  e.g., `void (*handler)(struct node *)`)
- [ ] Grouped declarators for function pointers (`int (*fp)(int)`)
  -- the parser has a TODO/heuristic here that may not work
- [ ] `static` local variables (declared but code generator may not
  emit them to .data/.bss)
- [ ] Multiple declarators in one statement (`int a, b, c;` -- partially
  implemented)
- [ ] Array initializers (`int a[] = {1, 2, 3}`)
- [ ] Struct initializers (`struct s x = {0}`)
- [ ] String literal concatenation (`"hello" " world"` -- parser skips
  adjacent strings instead of concatenating)

## Code Generator Gaps

- [ ] Variadic function calls (`printf(fmt, ...)` -- no code to handle
  more than 8 args or varargs ABI)
- [ ] `static` global/local variables (need `.local` directive for
  file-scope static)
- [ ] Large immediate values in `cmp` instruction (switch cases with
  values > 4095 need a `mov` + `cmp` sequence instead of immediate cmp)
- [ ] Struct return values (currently returns address in x0; some ABIs
  require copying)
- [ ] Proper `default:` case in switch (current code uses val=-1 sentinel,
  which won't match anything and won't jump to default)

## Preprocessor Gaps

- [ ] `#if` constant expressions involving defined() (partially done)
- [ ] `#error` directive
- [ ] Macro expansion in `#include` paths
- [ ] `__LINE__`, `__FILE__` inside macros (partially done)
- [ ] Token pasting `##` (partially done)
- [ ] Stringification `#` in macro bodies (partially done)
- [ ] Nested includes with pragma once across different paths

## Assembler Gaps (free-as)

Not fully audited, but the assembler needs to handle all instruction
forms that the code generator emits:
- [ ] All AArch64 load/store variants (ldrsb, ldrsh, ldrsw, etc.)
- [ ] `movz`/`movk` with shift amounts
- [ ] `stp`/`ldp` with pre/post-index
- [ ] `msub` instruction
- [ ] `.string` directive (for string literals)
- [ ] `.quad`, `.word`, `.short`, `.byte` directives
- [ ] `.section` with flags
- [ ] `.p2align` directive
- [ ] PC-relative addressing (`adrp` + `add`)
- [ ] Branch instructions with label references
- [ ] `.size` and `.type` directives

## Linker Gaps (free-ld)

- [ ] Relocation types: R_AARCH64_ADR_PREL_PG_HI21,
  R_AARCH64_ADD_ABS_LO12_NC, R_AARCH64_CALL26, R_AARCH64_JUMP26
- [ ] Multiple .o file linking with proper symbol resolution
- [ ] BSS section handling
- [ ] RODATA section handling (for string literals)
- [ ] Multiple .text sections merging

## Recommended Order of Attack

1. **Fix preprocessor for complex headers** -- Debug why `#include "free.h"`
   fails when the header has many macros. This unlocks testing everything else.

2. **Implement variadic function support** -- Provide a self-hosting
   `stdarg.h` and implement `va_start`/`va_arg`/`va_end` in the code
   generator. This is needed because the compiler itself uses variadic
   functions for error reporting and assembly emission.

3. **Fix function pointer parsing** -- The declarator parser has a known
   limitation with grouped declarators (`int (*fp)(int)`). The compiler
   uses some function pointers internally.

4. **Fix default case in switch** -- The code generator uses a sentinel
   value (-1) for `default:` which won't generate a jump to the default
   label. Needs proper handling.

5. **Test assembler coverage** -- Verify free-as can assemble all the
   instruction patterns free-cc generates.

6. **Test linker coverage** -- Verify free-ld can link multiple .o files
   with proper relocation handling.

7. **Run bootstrap** -- Once all the above work, run
   `./scripts/bootstrap.sh` and iterate on any remaining issues.

## File Summary

Key files for the self-hosting effort:

- `scripts/bootstrap.sh` -- 3-stage bootstrap script
- `src/cc/cc.c` -- compiler driver
- `src/cc/lex.c` -- lexer (tokenizer)
- `src/cc/pp.c` -- preprocessor (#include, #define, #if)
- `src/cc/parse.c` -- recursive descent parser
- `src/cc/type.c` -- type system
- `src/cc/gen.c` -- AArch64 code generator
- `src/cc/util.c` -- arena allocator, string utils, error handling
- `include/free.h` -- shared type definitions
- `src/libc/include/stdarg.h` -- needs self-hosting version
- `src/as/` -- assembler (lex.c, encode.c, emit.c, as.c)
- `src/ld/` -- linker (elf.c, reloc.c, layout.c, ld.c)
