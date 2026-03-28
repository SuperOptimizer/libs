# test/libc/ - libc tests

Unit tests for freestanding C library functions.

Tests cover: string functions (strlen, strcmp, memcpy, memset), memory allocation (malloc, free), formatted I/O (printf, sprintf), file operations, program startup.

Conventions: Each test links against the free libc. Tests print results and exit 0 on success. No dependency on host libc.
