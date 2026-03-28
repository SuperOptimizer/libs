# GCC Torture Test Results for free-cc

Date: 2026-03-14
Toolchain: free-cc (full pipeline: cc -> as -> ld)
Reference: gcc (system, -std=gnu89 -w)
Test suite: GCC C torture execute tests (gcc/testsuite/gcc.c-torture/execute/)

## Latest Results (Run 2 - after builtin + include fixes)

| Category   | Count | Percentage | vs Run 1 |
|------------|------:|------------|----------|
| Pass       |   885 |     52.6%  | +298     |
| Mismatch   |   221 |     13.1%  | +79      |
| Crash      |   520 |     30.9%  | -377     |
| Skip       |    58 |      3.4%  | same     |
| **Total**  |  1684 |    100.0%  |          |

**Pass rate (of compilable tests): 885 / (885 + 221) = 80.0%**

### What Changed Between Runs

1. Added `__builtin_abort` -> `bl abort` mapping in codegen
2. Added `__builtin_memcpy/memset/memcmp/memmove/strlen/strcmp/strcpy/strncpy/printf/sprintf/malloc/alloca/free/abs/exit` -> libc redirect
3. Added `-I src/libc/include` to torture runner so standard headers are found

Result: **587 -> 885 passes (+51% improvement)**

## Crash Analysis (520 compile failures)

### Root Cause Breakdown

| Category | Count | Description |
|----------|------:|-------------|
| Parse errors | 370 | C99/GCC extension syntax not supported |
| Linker errors | 47 | Remaining undefined references |
| Other | 103 | Assembler errors, codegen issues |

### Top Parse Error Patterns

| Error | Count | Likely Cause |
|-------|------:|-------------|
| expected ';', got token 2 | 183 | keyword-as-expr (typeof, __extension__) |
| expected ';', got token 43 | 120 | `{` in unexpected context (compound literals, stmt exprs) |
| expected expression, got token 43 | 61 | compound literal `(type){...}` |
| expected ';', got token 69 | 35 | for-loop declarations (C99) |
| expected ';', got token 81 | 29 | __attribute__ in unexpected position |
| expected ')', got token 2 | 29 | typeof in cast/sizeof |

## Mismatch Analysis (221 runtime mismatches)

### Exit Code Distribution

| Exit Code | Signal | Count | Meaning |
|-----------|--------|------:|---------|
| 134 | SIGABRT | 152 | Test self-check fails (codegen bug) |
| 139 | SIGSEGV | 28 | Segfault (bad pointer/stack) |
| 0 | - | 12 | Expected non-zero, got 0 |
| 124 | timeout | 11 | Infinite loop |
| 42 | - | 4 | Wrong return value |
| Other | various | 14 | Various wrong values |

### Key Observations

- **152 SIGABRT (69%)**: Tests compile but produce wrong results, triggering abort(). Pure codegen correctness bugs.
- **28 SIGSEGV (13%)**: Bad pointer codegen, struct access, or stack corruption.
- **12 wrong-zero (5%)**: Tests where gcc returns non-zero but free-cc returns 0 -- missing functionality that silently succeeds.
- **11 timeouts (5%)**: Infinite loops from incorrect branch/loop codegen.

## Baseline Results (Run 1 - before fixes)

| Category   | Count | Percentage |
|------------|------:|------------|
| Pass       |   587 |     34.9%  |
| Mismatch   |   142 |      8.4%  |
| Crash      |   897 |     53.3%  |
| Skip       |    58 |      3.4%  |

## Regression Tests

10 selected mismatch tests saved in `test/codegen/torture_regression_*.c`.

## Next Steps (Priority Order)

1. **Map `__builtin_prefetch` to no-op** -- causes linker failures (8 tests)
2. **Fix parse errors for C99/GCC extensions** -- 370 tests blocked:
   - Compound literals `(type){...}`
   - Designated initializers `.field = val`
   - `typeof` / `__typeof__`
   - Statement expressions `({...})`
   - `__extension__` keyword
   - `for` loop declarations
3. **Fix SIGABRT codegen bugs** -- 152 tests compile but produce wrong results
4. **Fix SIGSEGV codegen bugs** -- 28 tests crash at runtime

## How to Run

```sh
./test/torture/run_torture.sh           # normal run
./test/torture/run_torture.sh --verbose # show all results
CC=/path/to/free-cc ./test/torture/run_torture.sh  # custom compiler
```
