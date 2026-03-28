# src/objdump/ - ELF inspector and disassembler

Inspects ELF64 files and disassembles aarch64 machine code.

- **objdump.c** - Full implementation with flags: -h (headers), -t (symbols), -d (disassemble), -r (relocations), -s (hex dump)

Disassembler decodes: ADD, SUB, MUL, SDIV, AND, ORR, EOR, MOVZ/K/N, B, BL, B.cond, BR, BLR, RET, LDR, STR, LDRB, STRB, STP, LDP, CMP, CSET, SVC, NOP, ADRP, ADR.

Conventions: Output format similar to GNU objdump. Freestanding -- direct syscalls, static buffers.
