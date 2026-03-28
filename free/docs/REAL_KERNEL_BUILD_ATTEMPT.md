# Real Linux Kernel Build Attempt with free Toolchain

**Date:** 2026-03-14
**Kernel:** Linux 7.0.0-rc3 (torvalds/linux, shallow clone)
**Target:** arm64 (aarch64)
**Config:** tinyconfig
**Toolchain:** free-cc 0.1.0 at `/home/forrest/CLionProjects/free/build/`

## Summary

The free toolchain was tested against a real Linux kernel build. The build
reached the `prepare0` / `scripts` phase and successfully compiled one
kernel object file (`scripts/mod/empty.o`) before hitting a blocking
compiler bug. The build system integration required wrapper scripts to
handle unsupported flags and version queries.

**Result:** Build blocked at `scripts/mod/devicetable-offsets.s` due to
a parser bug with `sizeof()` inside `__attribute__` arguments.

## Fixes Applied During Testing

### 1. GCC Version Reporting (FIXED)

**File:** `src/cc/pp.c`
**Issue:** free-cc reported `__GNUC__=4, __GNUC_MINOR__=0` but kernel
requires GCC >= 8.1.0.
**Fix:** Changed to `__GNUC__=14, __GNUC_MINOR__=1, __GNUC_PATCHLEVEL__=0`.

### 2. `__signed__` / `__unsigned__` / `__volatile__` / `__const__` Keywords (FIXED)

**File:** `src/cc/lex.c`
**Issue:** GCC alternate keywords like `__signed__` were not recognized by
the lexer. The kernel uses `typedef __signed__ char __s8;` extensively in
`include/uapi/asm-generic/int-ll64.h`.
**Fix:** Added entries to `ext_keywords[]` table:
- `__signed__` / `__signed` -> `TOK_SIGNED`
- `__unsigned__` / `__unsigned` -> `TOK_UNSIGNED`
- `__volatile__` / `__volatile` -> `TOK_VOLATILE`
- `__const__` / `__const` -> `TOK_CONST`

Also fixed `ext_keywords` feature-gate logic: entries with `feat=0, feat2=0`
were incorrectly skipped (ok initialized to 0, never set to 1).

### 3. `__SIZEOF_INT128__` Predefined Macro (FIXED)

**File:** `src/cc/pp.c`
**Issue:** free-cc defined `__SIZEOF_INT128__` but does not support the
`__int128` type, causing parse errors in `include/uapi/linux/types.h`
when the kernel tried to `typedef __signed__ __int128 __s128`.
**Fix:** Removed the `__SIZEOF_INT128__` definition.

## Blocking Errors

### BUG-1: `sizeof()` inside `__attribute__` arguments (COMPILER - BLOCKING)

**Error:**
```
./include/linux/compiler_attributes.h:33:70: error: expected ';', got token kind 42
```

**Root cause:** The attribute argument parser cannot handle `sizeof(type)`
as an argument. This pattern appears in the kernel's `atomic_t` definition:

```c
/* include/linux/atomic/atomic-arch-fallback.h:187 */
typedef struct {
    int __aligned(sizeof(int)) counter;    /* expands to: */
    /* int __attribute__((__aligned__(sizeof(int)))) counter; */
} atomic_t;
```

The parser handles `__attribute__((__aligned__(4)))` but fails when the
argument is a `sizeof` expression. The attribute parser likely uses a
simplified expression evaluator that doesn't support `sizeof`.

**Affected kernel headers:**
- `include/linux/atomic/atomic-arch-fallback.h` (atomic_t, atomic64_t)
- Any header using `__aligned(sizeof(...))` pattern

**Proposed fix:** Extend the attribute argument parser in `src/cc/ext_attrs.c`
to handle full constant expressions including `sizeof`, `_Alignof`, casts,
and arithmetic. Alternatively, parse attribute arguments as general
expressions using the main expression parser.

**Also:** The error triggers an infinite loop in parser recovery -- the parser
reports the same error 20 times without advancing tokens. The error recovery
in struct member parsing needs to consume tokens to make progress.

### BUG-2: `-D` macro expansion not working in `-c` mode (COMPILER)

**Error:**
```
$ free-cc -DMY_TYPE=int -c test.c   # fails: MY_TYPE not expanded
$ free-cc -DMY_TYPE=int -E test.c   # works: MY_TYPE expanded correctly
```

**Root cause:** Object-like macros defined via `-D` that expand to keywords
(e.g., `-D__signed__=signed`) are not expanded when the compiler runs in
`-c` mode. The preprocessor and parser may not be fully integrated -- the
`-E` path correctly expands macros but the `-c` path does not apply macro
expansion before parsing in all contexts.

**Impact:** Medium. This was worked around by adding `__signed__` as a
lexer keyword, but many kernel headers rely on `-D` macros that expand to
type specifiers.

**Proposed fix:** Ensure the full preprocessing pipeline runs before parsing
in `-c` mode. The macro expansion engine should be identical for `-E` and
`-c` modes.

## Build System Integration Issues

### TOOL-1: No `--version` flag on any tool

**Affected:** free-ld, free-nm, free-ar, free-objcopy, free-objdump,
free-readelf, free-strip

**Issue:** The kernel build system (`scripts/ld-version.sh`,
`scripts/cc-version.sh`, `scripts/as-version.sh`) runs `$TOOL --version`
to detect tool versions. None of the free tools support `--version`.

**Workaround:** Wrapper scripts that intercept `--version` and output
GNU-compatible version strings.

**Proposed fix:** Add `--version` support to all tools. Output format must
match GNU binutils:
```
GNU ld (free-ld) 2.42
GNU ar (free-ar) 2.42
GNU nm (free-nm) 2.42
```

### TOOL-2: free-cc cannot read from stdin (`-`)

**Issue:** The kernel build system pipes C code to the compiler via stdin:
```sh
cat <<EOF | $CC -E -P -x c -
...
EOF
```
free-cc does not accept `-` as a filename to mean stdin.

**Workaround:** Wrapper script reads stdin to a temp file.

**Proposed fix:** In `src/cc/cc.c`, handle `-` as a special filename that
reads from file descriptor 0.

### TOOL-3: free-cc does not support `-P` flag

**Issue:** `-P` suppresses line number markers in preprocessor output.
The kernel build uses `$CC -E -P` to extract preprocessor output.

**Workaround:** Wrapper silently ignores `-P`.

**Proposed fix:** Add `-P` flag support to the preprocessor output mode.

### TOOL-4: free-ld does not support `-v` flag

**Issue:** The kernel Makefile probes linker capabilities using `ld -v`.
free-ld doesn't recognize `-v`.

**Workaround:** Wrapper intercepts `-v` and prints version.

**Proposed fix:** Add `-v` support to free-ld.

### TOOL-5: Dependency file generation via `-Wp,-MMD,file.d`

**Issue:** Kbuild passes `-Wp,-MMD,.file.d` to generate dependency files
for `fixdep`. free-cc supports this natively, but the wrapper was initially
stripping it. After fixing the wrapper, this works.

**Status:** Working (was a wrapper bug, not a tool bug).

### TOOL-6: Broken `.inst` assembler directive detection

**Message:**
```
arch/arm64/Makefile:33: Detected assembler with broken .inst; disassembly will be unreliable
```

**Issue:** The kernel tests whether the assembler correctly handles `.inst`
directives. free-as fails this test. The `.inst` directive emits a raw
32-bit instruction word and is heavily used in the arm64 kernel.

**Proposed fix:** Verify and fix `.inst` support in `src/as/`.

## Unsupported Compiler Flags (Filtered by Wrapper)

The following flags are passed by Kbuild but not supported by free-cc.
They were silently filtered by the wrapper script:

### Optimization flags
- `-Os`, `-O2`, `-Og` -- optimization levels
- `-fno-common`, `-fno-strict-aliasing`, `-fno-strict-overflow`
- `-fno-delete-null-pointer-checks`, `-fno-allow-store-data-races`
- `-fno-omit-frame-pointer`, `-fomit-frame-pointer`
- `-fno-optimize-sibling-calls`, `-fconserve-stack`
- `-fno-inline-functions-called-once`, `-fno-partial-inlining`
- `-fno-ipa-sra`, `-fno-ipa-cp-clone`, `-fno-ipa-bit-cp`
- `-fno-gcse`, `-fno-tree-vectorize`, `-fno-reorder-blocks`
- `-ftrivial-auto-var-init=zero`, `-fzero-init-padding-bits=all`
- `-fmin-function-alignment=4`

### Security flags
- `-fno-stack-protector`, `-fno-stack-clash-protection`
- `-fno-PIE`, `-fno-pie`

### Code generation flags
- `-fno-asynchronous-unwind-tables`, `-fno-unwind-tables`
- `-fno-exceptions`, `-fno-jump-tables`
- `-fno-builtin` (partially supported), `-fno-builtin-wcslen`
- `-fshort-wchar` (supported by free-cc, passed through)
- `-funsigned-char` (supported by free-cc, passed through)
- `-fvisibility=hidden`
- `-fms-extensions`, `-fstrict-flex-arrays=3`
- `-fverbose-asm`

### Target/arch flags
- `-mlittle-endian`
- `-mabi=lp64`
- `-mbranch-protection=none`
- `-mno-outline-atomics`
- `-Wa,-march=armv8.5-a`

### Warning flags
- `-Wundef`, `-Wmissing-declarations`, `-Wmissing-prototypes`
- `-Wframe-larger-than=2048`, `-Wvla-larger-than=1`
- `-Wimplicit-fallthrough=5`
- `-Werror=date-time`, `-Werror=implicit-function-declaration`
- `-Werror=implicit-int`, `-Werror=return-type`
- `-Werror=strict-prototypes`, `-Werror=incompatible-pointer-types`
- `-Werror=designated-init`
- `-Wcast-function-type`, `-Wenum-conversion`
- Many `-Wno-*` flags (type-limits, pointer-sign, etc.)

### Diagnostic flags
- `-fdiagnostics-show-context=2`
- `-fmacro-prefix-map=*`, `-ffile-prefix-map=*`

### Linker flags (in `-Wp,`)
- `--param=allow-store-data-races=0`

## What Successfully Compiled

1. **`scripts/mod/empty.o`** -- An empty C file with kernel headers force-included
   via `-include`. This validates that:
   - The preprocessor handles `-include` correctly
   - `-Wp,-MMD` dependency generation works
   - Basic kernel header inclusion works (when no `sizeof` in attributes)
   - `-nostdinc` with explicit `-I` paths works
   - `-D__KERNEL__` and other macro definitions work

## Recommended Priority of Fixes

1. **BUG-1: sizeof() in __attribute__ arguments** -- This is THE blocking issue.
   Every kernel file includes `<linux/types.h>` which includes `atomic_t`.
   Without this fix, zero kernel .c files can compile.

2. **BUG-2: -D macro expansion in -c mode** -- Important for correctness
   but partially worked around by adding keywords to the lexer.

3. **TOOL-1: --version on all tools** -- Simple to add, unblocks build
   system integration without wrapper scripts.

4. **TOOL-2: stdin support** -- Simple to add, needed for build system
   probing.

5. **TOOL-3: -P flag** -- Simple to add.

6. **TOOL-6: .inst directive** -- Needed for arm64 kernel assembly files.

7. **Unsupported compiler flags** -- Many can be silently ignored.
   Key ones to actually implement:
   - `-Os` / `-O2` (optimization)
   - `-fno-stack-protector` (code generation)
   - `-fPIC` / `-fno-PIE` (position independence)

## Methodology

### Wrapper Scripts

Wrapper scripts in `/tmp/free-wrappers/` intercept unsupported flags and
version queries. The wrappers:

1. Intercept `--version` to output GNU-compatible version strings
2. Intercept `-Wa,--version` for assembler version detection
3. Read stdin to temp file when `-` is passed as input
4. Filter ~60 unsupported compiler/linker flags
5. Pass through supported flags like `-Wp,-MMD`, `-fshort-wchar`, etc.

### Build Command

```sh
cd /tmp/linux
make ARCH=arm64 \
  "CC=/tmp/free-wrappers/free-cc" \
  HOSTCC=gcc \
  "AS=/tmp/free-wrappers/free-cc" \
  "LD=/tmp/free-wrappers/free-ld" \
  "AR=/tmp/free-wrappers/free-ar" \
  "NM=/tmp/free-wrappers/free-nm" \
  "OBJCOPY=/tmp/free-wrappers/free-objcopy" \
  "STRIP=/tmp/free-wrappers/free-strip" \
  "OBJDUMP=/tmp/free-wrappers/free-objdump" \
  "READELF=/tmp/free-wrappers/free-readelf" \
  -j1 -k
```

Note: `HOSTCC=gcc` is used for host tools (fixdep, conf, modpost, etc.)
since those run on the build machine and don't need cross-compilation.

## Code Changes Made

### `src/cc/pp.c`
- Changed `__GNUC__` from 4 to 14, `__GNUC_MINOR__` from 0 to 1
- Removed `__SIZEOF_INT128__` predefinition

### `src/cc/lex.c`
- Added `__signed__`, `__signed`, `__unsigned__`, `__unsigned`,
  `__volatile__`, `__volatile`, `__const__`, `__const` to `ext_keywords[]`
- Fixed feature-gate logic: entries with `feat=0, feat2=0` were skipped
  instead of being always-enabled
