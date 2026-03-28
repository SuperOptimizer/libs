# src/libc/ - Freestanding C library

Minimal C library for programs compiled by the free toolchain. No glibc dependency.

- **arch/** - Architecture-specific assembly (crt0, syscall stubs)
- **include/** - Public headers (string.h, stdlib.h, stdio.h, etc.)

Provides: string/memory functions, formatted I/O (printf, sprintf), malloc/free, file I/O wrappers around Linux syscalls, program startup (crt0).

Conventions: Pure C89 where possible, assembly only for syscall entry and crt0. Targets Linux aarch64 only.
