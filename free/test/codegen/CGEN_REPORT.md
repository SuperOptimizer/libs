# cgen Random Testing Report

**Date:** 2026-03-14
**Seeds tested:** 1-500
**Tool:** `./build/cgen <seed>` -> gcc vs free-cc comparison

## Summary (after label-at-end-of-block fix)

| Metric   | Count | Percentage |
|----------|-------|------------|
| Pass     | 341   | 68.2%      |
| Fail     | 94    | 18.8%      |
| Skip     | 3     | 0.6%       |
| Crash    | 62    | 12.4%      |

- **Pass**: gcc and free-cc produce same exit code
- **Fail**: Both compile successfully but exit codes differ (MISMATCH)
- **Skip**: cgen failed to generate, gcc failed to compile, or gcc binary timed out
- **Crash**: free-cc failed to compile valid C89 code

### Improvement from label fix

| Metric | Before fix | After fix | Change |
|--------|-----------|-----------|--------|
| Pass   | 137       | 341       | +204   |
| Fail   | 41        | 94        | +53    |
| Crash  | 318       | 62        | -256   |

The label-at-end-of-block parser fix (in `src/cc/parse.c`) resolved 256 crashes.
Many formerly-crashing programs now pass (204 new passes) or reveal new mismatches
(53 new mismatches were previously hidden by the crash).

## Fix Applied: label at end of compound statement

**Changed:** `src/cc/parse.c` — three locations:

1. **User labels** (`identifier :`): When a label is followed by `}`, insert an
   implicit empty `ND_BLOCK` instead of calling `parse_stmt()`.
2. **case labels** (`case expr :`): Same fix.
3. **default labels** (`default :`): Same fix.

This is a gcc extension (label at end of compound statement). gcc accepts it even
with `-std=c89`; only `-Wpedantic` produces a warning.

## Remaining Crash Analysis (62 crashes)

The remaining 62 crashes are from various causes (assembler errors, linker errors,
codegen producing invalid assembly). These need individual investigation.

## Mismatch Analysis (94 mismatches)

### Common constructs in mismatched programs

All mismatched programs heavily exercise:
- **Ternary operators** (`? :`) — often nested 2-3 deep
- **Comma operators** (`(expr, expr)`)
- **sizeof** casts — `(int)sizeof(type)` in arithmetic
- **Bit shifts** (`<<`, `>>`) — including large shift amounts
- **Nested switch statements** — with many cases
- **Unsigned/signed mixing** — `unsigned char`, `unsigned long` in expressions
- **Complex function call chains** — nested function calls as arguments

These suggest codegen bugs in one or more of:
1. Complex expression evaluation (register allocation under pressure)
2. Ternary operator codegen (branch/value merging)
3. Comma operator evaluation (side effects)
4. Integer promotion/conversion (signed vs unsigned)
5. Switch statement codegen (case dispatching in nested switches)

### Regression test files

Mismatch test cases saved as `test/codegen/cgen_regression_<seed>.c`.
To reproduce any seed:
```bash
./build/cgen <seed> > /tmp/test.c
gcc -std=c89 -pedantic -w -o /tmp/test_gcc /tmp/test.c && /tmp/test_gcc; echo "gcc=$?"
./build/free-cc -o /tmp/test_free /tmp/test.c && /tmp/test_free; echo "free=$?"
```

## Pass rate excluding crashes

Of programs that free-cc successfully compiled: **341/435 = 78%** matched gcc.
