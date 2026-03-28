# src/cc/ - C compiler

Compiles C89 source to aarch64 assembly or directly to ELF object files.

Pipeline: lexer (tokenize) -> parser (AST) -> codegen (aarch64 instructions).

Key structures defined in include/free.h: tokens (struct tok), AST nodes (struct node), types (struct type), symbols (struct symbol).

Conventions: Single-pass where possible. Arena allocation for all AST/type data. No intermediate representation beyond the AST. Error messages include file:line:col.
