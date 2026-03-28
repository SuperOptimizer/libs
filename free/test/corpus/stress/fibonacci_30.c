/* EXPECTED: 40 */
int fib(int n) {
    int a = 0, b = 1;
    int i, tmp;
    for (i = 0; i < n; i++) {
        tmp = a + b;
        a = b;
        b = tmp;
    }
    return a;
}

int main(void) {
    int result = fib(30);
    /* fib(30) = 832040, 832040 & 255 = 40 */
    return result & 255;
}
