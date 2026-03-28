/*
 * errno.c - Thread-local errno for the free libc.
 * Pure C89. No external dependencies.
 */

static int _errno_val = 0;

int *__errno_location(void)
{
    return &_errno_val;
}
