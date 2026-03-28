# src/ - Source code root

Each subdirectory is a separate tool in the free toolchain, compiled independently.

- **cc/** - C compiler (lexer, parser, codegen targeting aarch64)
- **as/** - Assembler (aarch64 assembly to ELF .o)
- **ld/** - Linker (combines .o files into ELF executables)
- **ar/** - Archive tool (creates/inspects static .a libraries)
- **objdump/** - ELF inspector and aarch64 disassembler
- **libc/** - Freestanding C library (syscall wrappers, string/memory, crt)

Conventions: Pure C89. All variables declared at top of block. /* */ comments only. No libc dependency -- each tool uses direct syscalls.
