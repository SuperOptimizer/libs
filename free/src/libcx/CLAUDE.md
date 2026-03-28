# src/libcx/ - Extended Utility Library

Extended C library ("libcx") with useful functions not found in standard C libraries. Built as libcx.a.

## Modules

- **arena.c / cx_arena.h** - Arena allocator (chunk-based, fast alloc/reset/destroy)
- **str.c / cx_str.h** - Dynamic string builder (append, format, slice, find)
- **vec.c / cx_vec.h** - Type-safe dynamic array via CX_VEC_DEFINE macro (header-only)
- **map.c / cx_map.h** - Hash map (string keys, void* values, open addressing, linear probing)
- **path.c / cx_path.h** - Path manipulation (join, dirname, basename, ext, normalize)
- **utf8.c / cx_utf8.h** - UTF-8 encode/decode/validate/iterate
- **log.c / cx_log.h** - Logging framework (levels, timestamps, file output)
- **base64.c / cx_base64.h** - Base64 encode/decode
- **hash.c / cx_hash.h** - Hash functions (CRC32, FNV-1a, MurmurHash3)
- **argparse.c / cx_argparse.h** - Command-line argument parser
- **cx_list.h** - Intrusive doubly-linked list macros (header-only)
- **cx_bitops.h** - Bit manipulation utilities (header-only)
- **cx.h** - Master header that includes all modules

## Conventions

- Pure C89 (`-std=c89 -pedantic`)
- All variables declared at top of block
- `/* */` comments only, no `//`
- 4-space indent, no tabs
- Headers in include/ subdirectory with CX_ prefix guards
- Functions that return allocated strings document caller-must-free
