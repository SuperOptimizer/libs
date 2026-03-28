# free — Standalone C89 Toolchain for AArch64 Linux

## Project Overview
A completely standalone, freestanding C89 toolchain: compiler, assembler, linker, libc, ar, objdump. Targets aarch64 Linux. Zero external dependencies. Inspired by tinygrad's philosophy of minimalism and self-containment.

## Build
```bash
mkdir build && cd build && cmake .. && make
make test          # run all tests
make test-libc     # libc tests only
make test-as       # assembler tests only
make test-ld       # linker tests only
make test-cc       # compiler tests only
```

## Code Standards
- **Language**: Pure C89 (`-std=c89 -pedantic`). No C99/C11 features. No GCC extensions in toolchain code (stdarg.h bootstrap exception).
- **Warnings**: `-Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wold-style-definition`
- **Style**: 4-space indent. No tabs. Opening brace on same line. Max ~100 chars per line.
- **Declarations**: All variables declared at top of block (C89 requirement).
- **Naming**: `snake_case` for functions/variables. `UPPER_CASE` for macros/constants. `CamelCase` for type names only when matching ELF spec (e.g., `Elf64_Ehdr`).
- **Headers**: Include guards with `#ifndef FREE_*_H` / `#ifndef _*_H` for libc.
- **Comments**: `/* C89 block comments only */`. No `//` line comments.

## Architecture
```
Source → Lexer → Preprocessor → Parser/AST → CodeGen → Assembly → Assembler → ELF .o → Linker → Executable
```

### Key Files
- `include/free.h` — Shared types: tokens, AST nodes, types, symbols, arena allocator
- `include/elf.h` — ELF64 structures and constants
- `include/aarch64.h` — AArch64 instruction encoders and register/syscall constants
- `src/cc/` — C compiler: lex.c, pp.c, parse.c, type.c, gen.c, cc.c
- `src/as/` — Assembler: lex.c, encode.c, emit.c, as.c
- `src/ld/` — Linker: elf.c, reloc.c, layout.c, ld.c
- `src/libc/` — Freestanding C library with arch/aarch64/ for asm
- `src/ar/` — Static archive tool
- `src/objdump/` — ELF inspector/disassembler

### Conventions
- Test-driven: write tests before or alongside implementation
- Each .c file should have a corresponding test file in test/
- Integration tests in test/integration/cases/ are numbered .c files compiled through the full pipeline
- Arena allocator for all compiler allocations (no malloc in cc/)
- Linked lists for sequences (tokens, AST nodes, symbols)
- Recursive descent parsing, no parser generators
- Multi-pass compilation (not single-pass like TCC)

### AArch64 Notes
- All instructions are 32-bit fixed width
- Stack must be 16-byte aligned at all times
- AAPCS64: args in x0-x7, return in x0, callee-saved x19-x28, FP=x29, LR=x30
- No plain `open`/`stat` syscalls on aarch64 — use `openat` (#56) and `fstatat` (#79)
- Syscall: number in x8, args in x0-x5, invoke with `svc #0`

### Bootstrap
- Phase 1: Build with system gcc/clang (current)
- Phase 2: Build free with free (self-hosting)
- Phase 3: Verify stage2 == stage3 (byte-identical)

## Testing
- Test framework in `test/test.h` — ASSERT, ASSERT_EQ, ASSERT_STR_EQ macros
- `make test` runs everything
- Integration tests compile .c → run binary → check exit code
- Target: 100% test coverage, fuzz all input parsers
- Each component (libc, as, ld, cc, ar, objdump) must be independently buildable and testable

## Development Workflow
- **Prefer agent teams** for multi-component work. Each component is independent — use separate agents/teammates per component (cc, as, ld, libc, ar, objdump) to avoid stepping on each other's toes.
- Components share only the headers in `include/` — no circular dependencies between `src/` subdirectories.
- When making cross-component changes, use agent teams so each teammate owns one component.
- Each subdirectory has its own CLAUDE.md with component-specific conventions.
