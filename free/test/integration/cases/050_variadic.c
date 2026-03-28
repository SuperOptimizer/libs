/* EXPECTED: 15 */
/* Test: variadic function support (va_start, va_arg, va_end) */

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

int sum(int n, ...)
{
    va_list ap;
    int i;
    int s;

    s = 0;
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        s = s + va_arg(ap, int);
    }
    va_end(ap);
    return s;
}

int main(void)
{
    return sum(5, 1, 2, 3, 4, 5);
}
