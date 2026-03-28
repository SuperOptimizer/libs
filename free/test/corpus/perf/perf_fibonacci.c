/* EXPECTED: 201 */
/* Recursive fibonacci(35), return low bits */
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    int r = fib(35);
    /* fib(35) = 9227465 */
    return r & 0xFF;
}
