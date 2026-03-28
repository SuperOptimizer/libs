# src/libc/arch/aarch64/ - AArch64 assembly for libc

Assembly source for Linux aarch64 libc components.

Expected files:
- **crt0.S** - Program entry (_start): set up argc/argv from stack, call main, exit
- **syscall.S** - Raw syscall stubs (syscall1 through syscall6)

Uses Linux aarch64 syscall ABI: syscall number in x8, args in x0-x5, return in x0. SVC #0 for syscall entry.

Conventions: .global for exported symbols. .text section. Minimal register usage.
