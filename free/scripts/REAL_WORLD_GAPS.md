# Real-World Compilation Gap Analysis

Tested the free-cc compiler against real-world C patterns and Lua 5.1.5 source.
Date: 2026-03-14

## Summary

**0/32 Lua source files compile** with free-cc. The blocking issues fall into
a small number of root causes, listed below by severity.

## Critical Gaps (block all real-world code)

### GAP-1: Backslash-newline continuation not supported (preprocessor)

**Category**: Preprocessor bug
**Impact**: Blocks assert.h, all multiline macros, most real headers
**Symptom**: `error: unexpected character '\' (92)`
**Example**:
```c
#define SWAP(a, b) \
    do { int tmp = (a); (a) = (b); (b) = tmp; } while (0)
```
**Files affected**: assert.h (line 8), every Lua file that includes it,
virtually all real-world C code uses multiline macros.

### GAP-2: Floating-point literals not lexed

**Category**: Lexer bug
**Impact**: Blocks any code using float/double constants
**Symptom**: `error: expected member name, got token kind 0` on `3.14`
**Example**:
```c
double d = 3.14;
float f = 3.14f;
```
**Note**: The `float` and `double` types themselves parse fine when assigned
integer values. Only the literal `3.14` / `3.14f` syntax fails.

### GAP-3: Function pointer syntax not parsed in declarations

**Category**: Parser bug
**Impact**: Blocks stdlib.h (qsort), signal.h, any callback-based API
**Symptom**: `error: expected ')', got token kind 40`
**Applies to**:
- Function pointer parameters: `void sort(int *arr, int (*cmp)(int, int))`
- Function pointer typedefs: `typedef int (*compare_fn)(int, int)`
- Function pointer return types: `sighandler_t signal(int sig, sighandler_t handler)`

**Note**: Function pointer *variables* and *calls through them* work fine
(`int (*op)(int, int); op = add; op(1, 2);` compiles). Only the declaration
syntax inside parameter lists and typedefs fails.

### GAP-4: Initializer lists not supported

**Category**: Parser bug (missing feature)
**Impact**: Blocks array/struct initialization, very common in C89 code
**Symptom**: `error: expected expression, got token kind 42` on `{`
**Example**:
```c
int arr[5] = {1, 2, 3, 4, 5};
struct point p = {10, 20};
int zeros[100] = {0};
```

### GAP-5: `__builtin_va_list` not recognized

**Category**: Missing compiler builtin
**Impact**: Blocks stdarg.h, stdio.h (printf), and all variadic functions
**Symptom**: `error: expected ';', got token kind 1` in stdarg.h line 9
**Root cause**: `typedef __builtin_va_list va_list;` — the compiler doesn't
recognize `__builtin_va_list` as a type. Also missing `__builtin_va_start`,
`__builtin_va_end`, `__builtin_va_arg`.

### GAP-6: Bitfield syntax not parsed

**Category**: Parser bug (missing feature)
**Impact**: Blocks code using bit-packed structs (common in embedded, protocol code)
**Symptom**: `error: expected ';', got token kind 47` on the `:` in bitfield
**Example**:
```c
struct flags {
    unsigned int a : 1;
    unsigned int b : 3;
};
```

## High-Impact Gaps

### GAP-7: Arena OOM with multiple `int` parameters

**Category**: Codegen bug (likely infinite loop or exponential blowup)
**Impact**: Blocks calling any function with 2+ `int`-typed parameters
**Symptom**: `arena_alloc: out of memory (4194440/4194304)`
**Trigger**: Specifically `int` + `int` parameter pairs. Other type
combinations work:
- `int f(int a)` — OK
- `int f(int a, int b)` — OOM
- `int f(int a, int b, int c)` — OOM
- `int f(int *a, int b)` — OK
- `int f(int a, int *b)` — OK
- `int f(char a, int b)` — OK
- `int f(long a, int b)` — OK
- `int f(int *a, int *b)` — OK

This is the most surprising bug. It appears to be a codegen or IR issue
where identical-type consecutive parameters trigger exponential behavior.

### GAP-8: Missing standard headers

**Category**: Missing libc headers
**Impact**: Blocks code that uses these features
**Missing headers**:
- `setjmp.h` — needed by Lua (error handling), many C programs
- `locale.h` — needed by Lua lexer, internationalization
- `math.h` — exists but may be incomplete (not tested deeply)
- `time.h` — exists but may be incomplete (not tested deeply)

### GAP-9: Preprocessor token pasting (`##`) not supported

**Category**: Preprocessor bug (missing feature)
**Impact**: Blocks many macro-heavy codebases
**Symptom**: `error: expected ';', got token kind 2`
**Example**:
```c
#define CONCAT(a, b) a ## b
int CONCAT(ma, in)(void) { return 42; }
```

## Working Features (confirmed)

The following C89 features compile successfully:
- Basic types: int, char, long, short, unsigned, signed, long long
- Pointer types, double pointers, void pointers
- Type casting between all types
- Structs (including nested, with array members, self-referential)
- Unions
- Enums (with explicit values)
- Typedef (simple types, struct aliases)
- All arithmetic operators (+, -, *, /, %)
- All comparison operators (==, !=, <, >, <=, >=)
- All bitwise operators (&, |, ^, ~, <<, >>)
- Logical operators (&&, ||, !)
- Compound assignment (+=, -=, *=, /=, %=, <<=, >>=, &=, |=, ^=)
- Increment/decrement (++, --)
- Ternary operator (?:)
- Comma operator (including in for loops)
- sizeof (types, expressions, struct types)
- Pointer arithmetic and subtraction
- Array indexing and array-to-pointer decay
- Struct member access (. and ->)
- Struct assignment (copy)
- Struct pass by value to functions
- String literals with escape sequences
- String literal concatenation ("hello " "world")
- Character literals with escapes
- Hex and octal integer literals
- Integer literal suffixes (L, U, UL)
- Control flow: if/else, while, do-while, for, switch/case/default, break, continue, goto
- Static global variables and functions
- Static local variables
- Const and volatile qualifiers
- Register keyword
- Extern declarations (including variadic `...`)
- Forward declarations
- Function calls (0 or 1 parameter)
- Nested function calls
- Chained assignment (a = b = c = 42)
- Empty statements
- Cast to void
- Anonymous structs
- Multi-dimensional arrays
- Array of pointers
- Preprocessor: #define, #undef, #ifdef, #ifndef, #if, #elif, #else, #endif
- Preprocessor: #include, #error, #line, #pragma
- Preprocessor: function-like macros (MAX(a,b))
- Preprocessor: defined() operator
- Preprocessor: #if with arithmetic expressions
- Preprocessor: nested macro expansion
- Preprocessor: stringification (#x)
- Preprocessor: __FILE__, __LINE__
- -D flag for predefining macros
- -std=c89/c99/c11 flag accepted

## Lua 5.1.5 Compilation Results

All 32 Lua source files failed. Root causes by file:

| File | First Error |
|------|-------------|
| lapi.c | GAP-1 (assert.h backslash continuation) |
| ldo.c | GAP-8 (missing setjmp.h) |
| llex.c | GAP-8 (missing locale.h) |
| loslib.c | GAP-8 (missing locale.h) |
| lua.c | GAP-3 (signal.h function pointer typedef) |
| lcode.c, lmathlib.c, loadlib.c | GAP-3 (stdlib.h qsort function pointer param) |
| All other files (22) | GAP-5 (stdarg.h __builtin_va_list) |

## Priority Recommendation

To unblock real-world code, fix in this order:
1. **GAP-1** (backslash continuation) — unblocks all multiline macros
2. **GAP-5** (va_list builtins) — unblocks stdio.h/printf
3. **GAP-2** (float literals) — unblocks numeric code
4. **GAP-3** (function pointer decls) — unblocks stdlib.h, callbacks
5. **GAP-4** (initializer lists) — unblocks idiomatic C initialization
6. **GAP-7** (arena OOM with int params) — unblocks most function calls
7. **GAP-9** (token pasting) — unblocks macro-heavy code
8. **GAP-6** (bitfields) — lower priority, fewer codebases need it
9. **GAP-8** (missing headers) — straightforward to add
