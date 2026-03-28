# free

A completely standalone, freestanding C89 toolchain for AArch64 Linux. Zero external dependencies. Compiler, assembler, linker, C library, and a full set of binary utilities -- everything needed to compile and link C programs from source to executable, without relying on any system toolchain.

Inspired by tinygrad's philosophy of minimalism and self-containment.

## Quick Start

```sh
# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Test
make test

# Install (to ~/.local/free by default)
cd ..
./scripts/install.sh

# Or install to a custom prefix
PREFIX=/opt/free ./scripts/install.sh
```

After installation, add the toolchain to your PATH:

```sh
export PATH="$HOME/.local/free/bin:$PATH"
```

## Tools

| Tool | Binary | Description |
|------|--------|-------------|
| C Compiler | `free-cc` | C89 compiler targeting AArch64 (with C99/C11/C23 extensions) |
| Preprocessor | `free-cpp` | Standalone C preprocessor |
| Assembler | `free-as` | AArch64 (and x86-64) assembler, produces ELF object files |
| Linker | `free-ld` | Static and dynamic ELF linker with linker script support |
| Archiver | `free-ar` | Creates and manipulates static libraries (.a) |
| Symbol Lister | `free-nm` | Lists symbols from object files and archives |
| Object Dump | `free-objdump` | ELF disassembler and inspector |
| Object Copy | `free-objcopy` | Copies and transforms object files |
| Strip | `free-strip` | Removes symbols from binaries |
| Size | `free-size` | Reports section sizes of object files |
| Strings | `free-strings` | Extracts printable strings from binaries |
| Addr2Line | `free-addr2line` | Maps addresses to source file and line numbers |
| ReadELF | `free-readelf` | Displays ELF file information |
| Debugger | `free-dbg` | Source-level debugger |
| Make | `free-make` | Minimal build system |
| Lex Dump | `free-lex` | Token dump tool (development/debugging) |
| AST Dump | `free-ast` | AST dump tool (development/debugging) |

Each tool is also available via a short symlink (e.g., `cc` -> `free-cc`).

## Libraries

| Library | File | Description |
|---------|------|-------------|
| libc | `libfree.a` | Freestanding C library (string, stdio, stdlib, math, etc.) |
| libcx | `libcx.a` | Extended utilities (arena allocator, vectors, maps, UTF-8, etc.) |

## Architecture

```
Source -> Lexer -> Preprocessor -> Parser/AST -> CodeGen -> Assembly
  -> Assembler -> ELF .o -> Linker -> Executable
```

The compiler is multi-pass with an intermediate representation (IR) that enables optimizations:

- **Frontend**: Lexer, preprocessor, recursive-descent parser, type checker
- **Middle**: IR generation, SSA-based optimizations (SCCP, DCE, mem2reg, inlining)
- **Backend**: Register allocation, AArch64/x86-64 code generation, DWARF debug info

All components are written in pure C89 with no external dependencies. The freestanding
libc provides everything needed -- system calls are made directly via `svc #0` on AArch64.

### Key Design Decisions

- **C89 throughout**: `-std=c89 -pedantic -Werror`. No GCC extensions in toolchain code.
- **Arena allocation**: All compiler memory is arena-allocated. No malloc in cc/.
- **Independent components**: Each tool builds and tests independently. No circular dependencies between src/ subdirectories.
- **Test-driven**: Every component has unit tests. Integration tests run the full pipeline.

### Bootstrap Plan

1. **Phase 1** (current): Build with system gcc/clang
2. **Phase 2**: Build free with free (self-hosting)
3. **Phase 3**: Verify stage2 == stage3 (byte-identical)

## Testing

```sh
# All tests
make test

# By component
make test-libc
make test-as
make test-ld
make test-cc
make test-libcx
make test-integration

# Verify installation
./scripts/test-install.sh
```

## Project Structure

```
include/          Shared headers (free.h, elf.h, aarch64.h, ...)
src/
  cc/             C compiler (lexer, preprocessor, parser, codegen, optimizer)
  as/             Assembler
  ld/             Linker
  libc/           Freestanding C library
    include/      libc public headers (stdio.h, stdlib.h, ...)
    arch/aarch64/ Architecture-specific assembly (crt, syscalls, setjmp)
  libcx/          Extended utility library
  ar/             Static archive tool
  nm/             Symbol listing tool
  objdump/        ELF inspector/disassembler
  objcopy/        Binary manipulation tool
  strip/          Symbol stripping tool
  size/           Section size reporter
  strings/        String extractor
  addr2line/      Address-to-source mapper
  readelf/        ELF file inspector
  dbg/            Debugger
  make/           Build system
  cpp/            Standalone preprocessor
  lex/            Token dump tool
  ast/            AST dump tool
test/             Tests for all components
scripts/          Build, install, and test scripts
```

## Requirements

- CMake 3.10+
- A C compiler (gcc or clang) for the initial bootstrap build
- AArch64 Linux target (cross-compilation works for the build itself)

## License

MIT License

Copyright (c) 2025-2026 free contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
