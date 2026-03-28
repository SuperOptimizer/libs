# test/cc/ - Compiler tests

Tests for the C compiler (free-cc).

Each test is a .c file compiled with free-cc. Expected behavior validated by running the output or comparing generated assembly.

Test categories: expressions, statements, control flow, functions, pointers, arrays, structs, type casting, preprocessor.

Conventions: One test per feature. Filename describes what is tested (e.g., arith.c, pointer.c). Exit code 0 = pass.
