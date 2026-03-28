# test/ - Test root

All tests for the free toolchain, organized by component.

- **cc/** - Compiler tests (C source -> expected output)
- **as/** - Assembler tests (assembly -> expected object files)
- **ld/** - Linker tests (linking scenarios, symbol resolution)
- **libc/** - libc function unit tests
- **integration/** - End-to-end tests (compile + assemble + link + run)
- **fuzz/** - Fuzz testing harnesses and seeds

Conventions: Each test is self-contained. Test scripts return 0 on success, non-zero on failure. Integration tests use the full toolchain pipeline.
