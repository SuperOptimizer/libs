# Repository Guidelines

## Project Structure & Module Organization
- `include/` holds shared headers for the toolchain (`free.h`, `elf.h`, `aarch64.h`, `x86_64.h`).
- `src/` contains the implementation, grouped by tool: `cc/`, `as/`, `ld/`, `libc/`, `libcx/`, `nm/`, `objdump/`, `objcopy/`, `readelf/`, `strip/`, `size/`, `strings/`, `addr2line/`, `dbg/`, `make/`, `cpp/`, `lex/`, and `ast/`.
- `src/libc/include/` is the freestanding libc header set; `src/libc/arch/aarch64/` contains runtime and syscall assembly.
- `test/` contains unit and integration tests. Component tests live in `test/<component>/test_*.c`; integration inputs are under `test/integration/cases/`. Fuzz corpora live in `test/fuzz/corpus/`, parser corpora in `test/corpus/`.
- Generated output belongs in `build/` and `stage1/`. Do not edit or commit those directories.

## Build, Test, and Development Commands
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` configures the project.
- `cmake --build build -j$(nproc)` builds the full toolchain.
- `cd build && make test` runs the full test suite.
- `cd build && make test-libc` / `test-as` / `test-ld` / `test-cc` / `test-libcx` / `test-integration` runs one area.
- `./scripts/install.sh [PREFIX]` installs to `~/.local/free` by default.
- `./scripts/test-install.sh [PREFIX]` verifies an installed toolchain.
- `./scripts/bootstrap.sh [build_dir]` runs the staged self-host check.

## Coding Style & Naming Conventions
- C89 throughout: 4-space indentation, no tabs, braces on the same line, declarations at the top of each block, and `/* ... */` comments in C sources.
- Use `snake_case` for functions and variables, `UPPER_CASE` for macros and constants, and `CamelCase` only where matching external data structures.
- Keep changes warning-clean under the repo’s strict flags: `-Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wold-style-definition`.

## Testing Guidelines
- Prefer adding or updating tests with the code change.
- Use `test/test.h` macros such as `TEST`, `ASSERT_EQ`, and `ASSERT_STR_EQ`; keep tests deterministic and small.
- Name new test files `test_<feature>.c`. For corpus-style inputs, follow the numbered naming used in `test/c-testsuite/` and `test/integration/cases/`.
- For parser, assembler, linker, and libc changes, run the smallest relevant `make test-*` target first, then `make test`.

## Commit & Pull Request Guidelines
- Git history only shows `Initial commit`, so no strict repository convention is established yet.
- Use short, imperative commit subjects with an optional scope, for example `cc: fix type promotion`.
- Pull requests should describe the change, list the validation commands you ran, and link any related issue. Include sample output when tool behavior changes.

## Component Notes
- Check the local `CLAUDE.md` files in the repo root, `src/*/`, and `test/*/` before editing a subsystem; they contain component-specific rules.
