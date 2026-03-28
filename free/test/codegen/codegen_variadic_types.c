/* EXPECTED: 42 */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

int test(int dummy, ...) {
    va_list ap;
    int a;
    long b;
    char *s;
    int *p;
    int val;
    int result;

    val = 100;
    va_start(ap, dummy);
    a = va_arg(ap, int);
    b = va_arg(ap, long);
    s = va_arg(ap, char *);
    p = va_arg(ap, int *);
    va_end(ap);

    result = a + (int)b + (s[0] - 'A') + (*p - 100);
    return result;
}

int main(void) {
    int val;
    val = 100;
    /* 10 + 20 + (0x4B - 0x41=10) + (100-100=0) + 2 from 'K'-'A' = 10 */
    /* Actually: a=10, b=20L, s[0]='M' (77-65=12), *p=val=100 -> 100-100=0 */
    /* 10 + 20 + 12 + 0 = 42 */
    return test(0, 10, 20L, "M", &val);
}
