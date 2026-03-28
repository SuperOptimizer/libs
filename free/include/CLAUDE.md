# include/ - Shared toolchain headers

All toolchain components include these headers. No libc dependency.

- **free.h** - Common types (u8, u16, u32, u64, etc), token/AST/type definitions, arena allocator, error handling, string utilities
- **elf.h** - ELF64 structures (Ehdr, Shdr, Phdr, Sym, Rela), AR archive header (Ar_hdr), relocation types, section/symbol constants
- **aarch64.h** - AArch64 register constants, condition codes, Linux syscall numbers, instruction encoder function declarations

Conventions: Pure C89. Typedefs for sized integers. All structs/enums defined here, not in source files.
