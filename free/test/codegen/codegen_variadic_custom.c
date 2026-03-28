/* EXPECTED: 42 */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

int max_of(int count, ...) {
    va_list ap;
    int i;
    int best;
    int cur;
    va_start(ap, count);
    best = va_arg(ap, int);
    for (i = 1; i < count; i++) {
        cur = va_arg(ap, int);
        if (cur > best) {
            best = cur;
        }
    }
    va_end(ap);
    return best;
}

int main(void) {
    return max_of(5, 10, 42, 3, 25, 7);
}
