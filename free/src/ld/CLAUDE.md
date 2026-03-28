# src/ld/ - Linker

Links ELF64 relocatable objects and static archives into executable ELF binaries.

Resolves symbols, applies relocations, lays out segments (LOAD for .text, .data, .bss).

Reads .a archives (AR format) and extracts only members that satisfy undefined references. Supports aarch64 relocation types defined in include/elf.h.

Conventions: Fixed base address (0x400000). Text segment first, then data. Entry point defaults to _start.
