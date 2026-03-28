/* GAP-5: __builtin_va_list not recognized
 * EXPECTED: compile success
 * STATUS: FAILS - stdarg.h parse error
 *
 * The compiler doesn't recognize GCC builtins needed for variadic functions:
 *   __builtin_va_list, __builtin_va_start, __builtin_va_end, __builtin_va_arg
 * This blocks stdarg.h, stdio.h (printf), and all variadic functions.
 *
 * For self-hosting, the compiler needs either:
 * (a) Support for __builtin_va_* as intrinsics, or
 * (b) A native va_list implementation for aarch64
 */
#include <stdarg.h>

int sum(int count, ...) {
    va_list args;
    int total = 0;
    int i;
    va_start(args, count);
    for (i = 0; i < count; i = i + 1) {
        total = total + va_arg(args, int);
    }
    va_end(args);
    return total;
}

int main(void) {
    return sum(3, 10, 20, 12);
}
