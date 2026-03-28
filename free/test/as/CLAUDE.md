# test/as/ - Assembler tests

Tests for the assembler (free-as).

Each test is a .s file assembled with free-as, then inspected with free-objdump to verify correct encoding.

Test categories: instruction encoding, directives, labels, relocations, section layout.

Conventions: One .s file per test. Verify with objdump -d for instruction encoding, -r for relocations, -h for sections.
