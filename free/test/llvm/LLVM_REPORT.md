# LLVM Test Suite Results for free-cc

**Date**: 2026-03-14
**Compiler**: free-cc (build/free-cc)
**Target**: aarch64-linux
**Test source**: LLVM test-suite SingleSource + LLVM clang/test/CodeGen

## Summary

| Metric | Count |
|--------|-------|
| Total test files | 51 |
| **Full pass (compile + run + output match)** | **32 / 51 (62.7%)** |
| Compile success | 41 / 51 (80.4%) |
| Run success (correct exit code) | 39 / 41 (95.1%) |
| Output match | 32 / 39 (82.1%) |
| Compile failures | 10 |
| Crashes (SIGSEGV) | 1 |
| Exit code mismatches | 1 |
| Output mismatches | 7 |

## Fully Passing Tests (32)

These compile, run, and produce identical output to gcc:

- `2002-04-17-PrintfChar` - printf with char arguments
- `2002-05-02-CastTest3` - integer casts
- `2002-05-03-NotTest` - bitwise NOT
- `2002-08-02-CastTest` - pointer/integer casts
- `2002-08-02-CastTest2` - more cast tests
- `2002-08-19-CodegenBug` - codegen edge case
- `2002-10-13-BadLoad` - load correctness
- `2003-04-22-Switch` - switch statements
- `2003-05-02-DependentPHI` - dependent control flow
- `2003-05-12-MinIntProblem` - INT_MIN handling
- `2003-05-14-AtExit` - atexit() callbacks
- `2003-07-06-IntOverflow` - integer overflow behavior
- `2003-07-08-BitOpsTest` - bitwise operations
- `2003-07-10-SignConversions` - signed/unsigned conversion
- `2003-08-20-FoldBug` - constant folding
- `2003-09-18-BitFieldTest` - bitfield operations
- `2003-10-13-SwitchTest` - complex switch
- `2005-05-13-SDivTwo` - signed division by 2
- `2005-07-15-Bitfield-ABI` - bitfield ABI compliance
- `Bubblesort` - Stanford benchmark: bubble sort
- `hello` - basic printf/puts
- `IntMM` - Stanford benchmark: integer matrix multiply
- `matrix` - Shootout benchmark: matrix operations
- `methcall` - Shootout benchmark: method calls via function pointers
- `objinst` - Shootout benchmark: object instantiation via structs
- `Perm` - Stanford benchmark: permutations
- `Puzzle` - Stanford benchmark: puzzle solver
- `Queens` - Stanford benchmark: N-queens
- `Quicksort` - Stanford benchmark: quicksort
- `TestLoop` - basic loop test
- `Towers` - Stanford benchmark: towers of hanoi
- `Treesort` - Stanford benchmark: tree sort

## Compile Failures (10)

### Assembler/Linker failures (4)
- `2002-10-09-ArrayResolution` - assembler: duplicate symbol `Array` (codegen emits duplicate labels)
- `2003-10-29-ScalarReplBug` - linker: unsupported relocation type
- `2006-01-29-SimpleIndirectCall` - linker: unsupported relocation for indirect call
- `2007-04-25-weak` - linker: `__attribute__((weak))` not supported

### Parse failures (4)
- `2002-12-13-MishaTest` - cannot parse `typedef` in certain contexts
- `2004-11-28-GlobalBoolLayout` - member access on typedef'd struct type not resolved
- `2007-01-04-KNR-Args` - K&R style function definitions not supported
- `RealMM` - `#define` with token-pasting not handled

### Semantic failures (2)
- `2006-01-23-UnionInit` - designated initializers in union initialization
- `2007-03-02-VaCopy` - `va_copy` not implemented

## Crash (1)
- `2009-04-16-BitfieldInitialization` - SIGSEGV during bitfield initialization at runtime

## Exit Code Mismatch (1)
- `lists` - gcc times out (>10s), free-cc completes instantly (likely different loop bounds with SMALL_PROBLEM_SIZE)

## Output Mismatches (7)

### Struct argument passing ABI (2)
- `2002-10-12-StructureArgs` - struct with float members passed by value: wrong values
- `2002-10-12-StructureArgsSimple` - simple struct with 2 doubles: garbage values (struct ABI bug)

### Floating-point codegen (3)
- `2004-02-02-NegativeZero` - negative zero not preserved (`-0.0` becomes `0.0`)
- `2006-12-01-float_varg` - float varargs: wrong register passing for float variadic args
- `FloatMM` - floating-point matrix multiply produces inf/nan (accumulation bug)

### Bitfield initialization (1)
- `2004-06-20-StaticBitfieldInit` - static bitfield initializers produce wrong values

### Integer arithmetic (1)
- `ary3` - large integer accumulation off by factor of 10 (likely int overflow or loop bound)

## Key Findings

### What works well
1. **Basic integer codegen** is solid: arithmetic, bitwise ops, casts, comparisons all correct
2. **Control flow** works: switch, loops, if/else, function calls
3. **Struct layout and access** works for simple cases
4. **Memory operations**: malloc/calloc/free, array indexing all work
5. **Stanford benchmarks** (classic algorithms) mostly pass: sorting, tree operations, permutations, matrix multiply (integer)

### Areas needing work (priority order)
1. **Struct-by-value ABI for float members** - registers for float struct members are wrong
2. **Float variadic argument passing** - float args in varargs use wrong registers
3. **Negative zero handling** - `-0.0` is not preserved
4. **Floating-point accumulation** - FloatMM produces inf/nan
5. **Static bitfield initializers** - wrong values emitted
6. **Designated initializers** - union init with designators unsupported
7. **K&R function definitions** - old-style parameter declarations not parsed
8. **`__attribute__((weak))`** - weak symbols not supported in linker
9. **`va_copy`** - not implemented

## Test Provenance

Tests sourced from:
- `llvm-test-suite/SingleSource/Benchmarks/Stanford/` - Stanford integer benchmark suite
- `llvm-test-suite/SingleSource/Benchmarks/Shootout/` - Programming Language Shootout
- `llvm-test-suite/SingleSource/UnitTests/` - LLVM unit tests
- `llvm-project/clang/test/CodeGen/` - Clang codegen regression tests (non-FileCheck subset)

Filtering criteria: must have `main()`, no SIMD/vector intrinsics, no C99-only headers (`stdint.h`, `stdbool.h`), no `long long`, no ObjC blocks, no MSVC extensions.
