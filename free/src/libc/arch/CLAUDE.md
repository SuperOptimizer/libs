# src/libc/arch/ - Architecture-specific assembly

Contains per-architecture subdirectories with assembly source that cannot be written in C.

- **aarch64/** - AArch64 (ARM64) Linux implementation

Each architecture provides: crt0 startup code (_start), raw syscall wrappers, setjmp/longjmp if needed.

Conventions: One file per logical unit. GNU as syntax. Use register names from include/aarch64.h constants.
