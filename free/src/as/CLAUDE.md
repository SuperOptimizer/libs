# src/as/ - Assembler

Assembles aarch64 assembly source into ELF64 relocatable object files (.o).

Two passes: first pass collects labels and sizes, second pass encodes instructions and emits relocations.

Uses instruction encoders from include/aarch64.h. Outputs ELF structures from include/elf.h.

Conventions: Labels on column 0, instructions indented. Directives start with '.'. Supports .text, .data, .bss, .rodata sections. Generates RELA relocations for unresolved symbols.
