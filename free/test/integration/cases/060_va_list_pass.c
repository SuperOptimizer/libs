/* EXPECTED: 42 */
/* Test: passing va_list as a function argument */
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_arg(v,l) __builtin_va_arg(v,l)
#define va_end(v) __builtin_va_end(v)

int vsum(int count, va_list ap) {
    int i, total;
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
    return sum(4, 10, 12, 8, 12);
}
