# test/fuzz/ - Fuzz tests

Fuzz testing harnesses for toolchain components.

Targets: lexer (malformed C input), parser (syntactically odd programs), assembler (random instruction streams), linker (corrupt ELF input), ar (malformed archives).

Conventions: Each harness reads from stdin or a file, exercises one component, should not crash. Seed corpus in subdirectories. Designed for use with simple random mutation or AFL-style fuzzing.
