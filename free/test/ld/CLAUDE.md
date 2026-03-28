# test/ld/ - Linker tests

Tests for the linker (free-ld).

Tests cover: symbol resolution, relocation application, multiple object file linking, archive library linking, entry point setup, segment layout.

Conventions: Tests assemble .s files with free-as, link with free-ld, verify output with free-objdump or by executing the result. Test scripts check exit codes and output.
