/* EXPECTED: 42 */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

int vsum(int count, va_list ap) {
    int i;
    int total;
    total = 0;
    for (i = 0; i < count; i++) {
        total = total + va_arg(ap, int);
    }
    return total;
}

int sum(int count, ...) {
    va_list ap;
    int result;
    va_start(ap, count);
    result = vsum(count, ap);
    va_end(ap);
    return result;
}

int main(void) {
    return sum(4, 10, 11, 12, 9);
}
