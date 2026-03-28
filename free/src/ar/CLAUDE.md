# src/ar/ - Archive tool

Creates and inspects static archives (.a files) in Unix AR format.

- **ar.c** - Full implementation: create (rcs), list (t), extract (x) operations

Supports long filenames via "//" extended name section. Generates symbol table ("/" member) for ranlib functionality. Parses and writes Ar_hdr fields as space-padded ASCII strings.

Conventions: Freestanding -- uses direct Linux aarch64 syscalls. Static buffers, no heap allocation. Big-endian u32 in symbol table per AR spec.
