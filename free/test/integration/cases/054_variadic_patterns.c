/* EXPECTED: 0 */
/* Test: variadic function patterns - multiple va_arg calls,
 * 1/4/8/9+ arguments (9th goes on stack per AAPCS64) */

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

/* Pattern 1: simple sum with multiple va_arg calls */
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

/* Pattern 2: multiple va_arg calls with different logic */
int first_and_last(int n, ...)
{
    va_list ap;
    int first;
    int last;
    int i;

    va_start(ap, n);
    first = va_arg(ap, int);
    last = first;
    for (i = 1; i < n; i++) {
        last = va_arg(ap, int);
    }
    va_end(ap);
    return first * 1000 + last;
}

int main(void)
{
    int ok;
    ok = 1;

    /* 1 arg */
    if (sum(1, 42) != 42) ok = 0;

    /* 4 args */
    if (sum(4, 1, 2, 3, 4) != 10) ok = 0;

    /* 7 args (fills x1-x7, all register args) */
    if (sum(7, 1, 2, 3, 4, 5, 6, 7) != 28) ok = 0;

    /* 8 args (n in x0, args in x1-x7 + 1 on stack) */
    if (sum(8, 1, 2, 3, 4, 5, 6, 7, 8) != 36) ok = 0;

    /* 9 args (n in x0, args in x1-x7 + 2 on stack) */
    if (sum(9, 1, 2, 3, 4, 5, 6, 7, 8, 9) != 45) ok = 0;

    /* 10 args (3 on stack) */
    if (sum(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55) ok = 0;

    /* first_and_last pattern */
    if (first_and_last(1, 42) != 42042) ok = 0;
    if (first_and_last(3, 10, 20, 30) != 10030) ok = 0;

    return ok ? 0 : 1;
}
