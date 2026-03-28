# Assembly Diff Report: gcc -O0 vs free-cc

Generated: 2026-03-14

Compiler versions:
- GCC: Ubuntu 15.2.0-12ubuntu1 (aarch64-linux-gnu-gcc)
- free-cc: built from current tree

Both compiled with `-std=gnu89 -O0 -S` (gcc) / `-S` (free-cc).

## Overall Statistics

| Metric | GCC -O0 | free-cc | Ratio |
|--------|---------|---------|-------|
| Total lines | 1319 | 2043 | 1.54x |
| Total instructions | 424 | 973 | 2.29x |
| Tests compiled | 53 | 53 | - |

free-cc produces **2.29x** more instructions than gcc -O0 on average.

## Full Test Comparison

| Test | GCC lines | free-cc lines | Line ratio | GCC instr | free-cc instr | Instr ratio |
|------|-----------|---------------|------------|-----------|---------------|-------------|
| 000_return_zero | 16 | 23 | 1.44 | 3 | 7 | 2.33 |
| 001_return_nonzero | 16 | 23 | 1.44 | 3 | 7 | 2.33 |
| 002_add | 16 | 27 | 1.69 | 3 | 11 | 3.67 |
| 003_sub | 16 | 27 | 1.69 | 3 | 11 | 3.67 |
| 004_mul | 16 | 27 | 1.69 | 3 | 11 | 3.67 |
| 005_div | 16 | 27 | 1.69 | 3 | 11 | 3.67 |
| 006_mod | 16 | 28 | 1.75 | 3 | 12 | 4.00 |
| 007_neg | 16 | 31 | 1.94 | 3 | 15 | 5.00 |
| 008_parens | 16 | 31 | 1.94 | 3 | 15 | 5.00 |
| 009_precedence | 16 | 31 | 1.94 | 3 | 15 | 5.00 |
| 010_local_var | 22 | 32 | 1.45 | 7 | 14 | 2.00 |
| 011_multi_var | 26 | 44 | 1.69 | 11 | 24 | 2.18 |
| 012_assign_expr | 22 | 29 | 1.32 | 7 | 12 | 1.71 |
| 013_if_true | 16 | 29 | 1.81 | 3 | 12 | 4.00 |
| 014_if_false | 16 | 29 | 1.81 | 3 | 12 | 4.00 |
| 015_if_else | 16 | 32 | 2.00 | 3 | 14 | 4.67 |
| 016_eq | 16 | 28 | 1.75 | 3 | 12 | 4.00 |
| 017_ne | 16 | 28 | 1.75 | 3 | 12 | 4.00 |
| 018_lt | 16 | 28 | 1.75 | 3 | 12 | 4.00 |
| 019_gt | 16 | 28 | 1.75 | 3 | 12 | 4.00 |
| 020_le | 16 | 28 | 1.75 | 3 | 12 | 4.00 |
| 021_ge | 16 | 28 | 1.75 | 3 | 12 | 4.00 |
| 022_while | 35 | 78 | 2.23 | 18 | 50 | 2.78 |
| 023_for | 35 | 78 | 2.23 | 18 | 50 | 2.78 |
| 024_do_while | 28 | 57 | 2.04 | 12 | 33 | 2.75 |
| 025_nested_if | 16 | 33 | 2.06 | 3 | 15 | 5.00 |
| 026_and | 16 | 33 | 2.06 | 3 | 15 | 5.00 |
| 027_or | 16 | 33 | 2.06 | 3 | 15 | 5.00 |
| 028_not | 16 | 25 | 1.56 | 3 | 9 | 3.00 |
| 029_bitand | 16 | 27 | 1.69 | 3 | 11 | 3.67 |
| 030_bitor | 16 | 27 | 1.69 | 3 | 11 | 3.67 |
| 031_bitxor | 16 | 27 | 1.69 | 3 | 11 | 3.67 |
| 032_shl | 16 | 27 | 1.69 | 3 | 11 | 3.67 |
| 033_shr | 16 | 27 | 1.69 | 3 | 11 | 3.67 |
| 034_ternary | 16 | 30 | 1.88 | 3 | 12 | 4.00 |
| 035_comma | 16 | 23 | 1.44 | 3 | 7 | 2.33 |
| 036_func_call | 46 | 56 | 1.22 | 17 | 28 | 1.65 |
| 037_recursion | 65 | 75 | 1.15 | 28 | 45 | 1.61 |
| 038_pointer | 48 | 40 | 0.83 | 28 | 20 | 0.71 |
| 039_array | 53 | 86 | 1.62 | 33 | 64 | 1.94 |
| 040_struct | 26 | 46 | 1.77 | 11 | 26 | 2.36 |
| 041_sizeof_int | 16 | 23 | 1.44 | 3 | 7 | 2.33 |
| 042_sizeof_long | 16 | 23 | 1.44 | 3 | 7 | 2.33 |
| 043_sizeof_ptr | 16 | 23 | 1.44 | 3 | 7 | 2.33 |
| 044_char | 22 | 32 | 1.45 | 7 | 14 | 2.00 |
| 045_string | 29 | 40 | 1.38 | 9 | 20 | 2.22 |
| 046_global | 30 | 39 | 1.30 | 10 | 16 | 1.60 |
| 047_multi_func | 44 | 54 | 1.23 | 15 | 27 | 1.80 |
| 048_inc_dec | 25 | 44 | 1.76 | 10 | 24 | 2.40 |
| 049_switch | 37 | 48 | 1.30 | 18 | 26 | 1.44 |
| 050_variadic | 134 | 152 | 1.13 | 95 | 109 | 1.15 |
| 051_bitfield | 34 | 59 | 1.74 | 19 | 35 | 1.84 |
| 052_string_concat | 30 | 40 | 1.33 | 10 | 20 | 2.00 |

## Top 5 Worst-Ratio Tests (Most Bloated)

| Rank | Test | GCC instr | free-cc instr | Ratio |
|------|------|-----------|---------------|-------|
| 1 | 007_neg | 3 | 15 | 5.00x |
| 2 | 008_parens | 3 | 15 | 5.00x |
| 3 | 009_precedence | 3 | 15 | 5.00x |
| 4 | 025_nested_if | 3 | 15 | 5.00x |
| 5 | 026_and / 027_or | 3 | 15 | 5.00x |

Note: GCC folds constant expressions like `-(- 42)` to `mov w0, 42` at -O0, so the high
ratios on expression-only tests reflect GCC's constant folding more than free-cc being
unusually bloated. The more meaningful metric is larger tests (loops, functions).

## Top 5 Worst by Absolute Instruction Overhead

| Test | GCC instr | free-cc instr | Extra instr |
|------|-----------|---------------|-------------|
| 039_array | 33 | 64 | +31 |
| 022_while | 18 | 50 | +32 |
| 023_for | 18 | 50 | +32 |
| 024_do_while | 12 | 33 | +21 |
| 037_recursion | 28 | 45 | +17 |

## Notable: free-cc beats GCC on 038_pointer

038_pointer: free-cc produces 20 instructions vs GCC's 28. GCC inserts stack canary
checks (`__stack_chk_guard` / `__stack_chk_fail`) for this test because it detects
a local array/pointer pattern. free-cc does not emit stack protector code, which is
correct for a freestanding compiler but means the comparison is apples-to-oranges here.

## Codegen Patterns Causing Bloat

### 1. Stack-based expression evaluation (biggest impact)

free-cc uses the stack as a virtual register for all binary operations:

```asm
/* free-cc: a + b */
str x0, [sp, #-16]!    /* push left operand */
...compute right...
ldr x1, [sp], #16      /* pop left operand */
add x0, x0, x1
```

GCC keeps values in registers:

```asm
/* gcc: a + b */
ldr w1, [sp, 8]
ldr w0, [sp, 12]
add w0, w1, w0
```

**Impact**: 94 stack push/pop pairs across all tests. Each pair is 2 extra instructions
(str + ldr) that could be eliminated by using registers directly. This is the single
largest source of bloat.

### 2. Redundant address recalculation

free-cc recalculates variable addresses every time they are referenced:

```asm
/* free-cc: every reference to 'i' recomputes its address */
sub x0, x29, #4        /* addr of 'i' */
ldrsw x0, [x0]
...
sub x0, x29, #4        /* addr of 'i' again */
str w0, [x1]
```

GCC loads/stores directly:

```asm
/* gcc: direct offset access */
ldr w0, [sp, 8]
str w0, [sp, 8]
```

**Impact**: Worst in loops - 022_while has 9 redundant `sub x0, x29` for just 2
variables. Each recalculation adds 1-2 extra instructions.

### 3. Unnecessary frame pointer setup in leaf functions

free-cc emits full prologue/epilogue for every function, including leaf functions
(functions that make no calls):

```asm
/* free-cc: even for `return 0` */
stp x29, x30, [sp, #-16]!
mov x29, sp
...
mov sp, x29
ldp x29, x30, [sp], #16
ret
```

GCC omits the frame for trivial leaf functions:

```asm
/* gcc: leaf function */
mov w0, 0
ret
```

**Impact**: 49 out of 53 tests are leaf functions (only main, no calls). Each gets
4 unnecessary instructions (stp, mov, mov, ldp). That is 196 wasted instructions
across the test suite.

### 4. 64-bit registers for 32-bit int operations

free-cc uses x-registers (64-bit) for all `int` operations. GCC uses w-registers
(32-bit) which is correct for C `int` (32 bits on aarch64).

| | w-register uses | x-register uses |
|----------|-----------------|-----------------|
| GCC | 186 | 107 |
| free-cc | 47 | 851 |

free-cc uses `mov x0, #42` and `add x0, x0, x1` where GCC uses `mov w0, 42` and
`add w0, w1, w0`. While functionally equivalent (the kernel only looks at w0 for
exit status), this:
- Wastes the sign-extension semantics (forces `ldrsw` everywhere)
- May confuse debuggers expecting w0 for int returns
- Prevents future use of w-register peephole optimizations

### 5. No constant folding

GCC at -O0 folds constant expressions like `-(-42)` to `mov w0, 42`. free-cc
evaluates them at runtime:

```asm
/* free-cc: -(-42) */
mov x0, #42
str x0, [sp, #-16]!
mov x0, #0
ldr x1, [sp], #16
sub x0, x0, x1       /* 0 - 42 = -42 */
str x0, [sp, #-16]!
mov x0, #0
ldr x1, [sp], #16
sub x0, x0, x1       /* 0 - (-42) = 42 */
```

**Impact**: Adds 8+ instructions for double-negation. Applies to all constant
expressions in conditions, initializers, array indices, etc.

### 6. Comparison via cset instead of direct branch

free-cc materializes comparison results into a register, then branches on the
register. GCC branches directly on the comparison flags:

```asm
/* free-cc: if (i < 10) */
cmp x0, x1
cset x0, lt
cmp x0, #0
b.eq .L.break

/* gcc: if (i < 10) */
cmp w0, 9
ble .L3
```

**Impact**: 2 extra instructions per comparison (cset + second cmp). Present in
every conditional and loop test.

## Correctness Issues

No correctness issues were found. All 53 tests compile and the assembly, while
bloated, produces correct results. However, there are two areas to watch:

1. **64-bit int operations**: Using x-registers for int arithmetic is not wrong
   per AAPCS64 (the upper 32 bits are ignored by the caller) but may cause
   subtle issues with sign extension if a function returns a negative int and
   the caller reads x0 as a 64-bit value.

2. **Missing stack protector**: free-cc does not emit `__stack_chk_guard` checks.
   This is expected for a freestanding compiler but means buffer overflows in
   compiled code will not be detected.

## Optimization Priority (by estimated instruction savings)

1. **Register allocation** instead of stack-based evaluation: ~188 instructions saved
2. **Leaf function optimization** (skip frame setup): ~196 instructions saved
3. **Direct memory addressing** (ldr/str with fp offset): ~60 instructions saved
4. **Use w-registers for int**: ~40 ldrsw eliminated, cleaner output
5. **Constant folding**: variable savings, biggest on expression-heavy code
6. **Direct conditional branches**: ~20 instructions saved (cset elimination)

Total estimated savings: ~500+ instructions (~50% reduction), bringing free-cc
close to gcc -O0 output size.
