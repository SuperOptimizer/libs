# scripts/ - Build and CI scripts

Build system and continuous integration scripts for the free toolchain.

Expected scripts:
- **build.sh** - Build all toolchain components (compiles each tool with the host or self-hosted compiler)
- **test.sh** - Run the full test suite
- **bootstrap.sh** - Self-hosting bootstrap (build free with free)

Conventions: Shell scripts (POSIX sh). No Make/CMake dependency. Scripts should work from the repo root. Exit non-zero on any failure.
