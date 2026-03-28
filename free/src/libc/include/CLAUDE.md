# src/libc/include/ - libc public headers

Headers installed for programs compiled with the free toolchain.

Expected headers:
- **string.h** - memcpy, memset, strlen, strcmp, strcpy, strncpy, memcmp
- **stdlib.h** - malloc, free, exit, atoi, abs
- **stdio.h** - printf, fprintf, sprintf, fopen, fclose, fread, fwrite, putchar
- **unistd.h** - read, write, open, close, lseek, brk, mmap, munmap

Conventions: Match standard C89 signatures. Minimal definitions -- no POSIX extras. Types from include/free.h.
