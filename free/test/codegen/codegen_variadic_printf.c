/* EXPECTED: 42 */
/* Test variadic with multiple args like printf would use */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

int sum_ints(int count, ...) {
    va_list ap;
    int i;
    int total;
    total = 0;
    va_start(ap, count);
    for (i = 0; i < count; i++) {
        total = total + va_arg(ap, int);
    }
    va_end(ap);
    return total;
}

int main(void) {
    return sum_ints(6, 1, 2, 3, 7, 11, 18);
}
