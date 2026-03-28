# src/dbg/ - Standalone C debugger

Mini-GDB for aarch64 Linux using ptrace. Reads DWARF debug info from ELF.

- **dbg.c** - Full implementation: ptrace process control, breakpoints, DWARF line parser, disassembler

Features: break (by function/file:line/address), run, continue, step, next (step over), print (registers/symbols/memory), backtrace (frame pointer walk), disassemble, examine memory, list source, info registers/breakpoints.

Uses BRK #0 (0xD4200000) for software breakpoints. Reads .debug_line for source mapping, .symtab/.strtab for symbol lookup. Walks x29 frame pointer chain for backtraces.

Conventions: Freestanding -- uses direct Linux aarch64 syscalls (ptrace=117, clone=220, execve=221, wait4=260). Static buffers, no heap allocation.
