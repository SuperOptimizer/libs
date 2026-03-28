/* EXPECTED: 140 */
int factorial(int n) {
    int result = 1;
    int i;
    for (i = 2; i <= n; i++) {
        result = result * i;
    }
    return result;
}

int main(void) {
    int result = factorial(12);
    /* 12! = 479001600, (479001600 >> 16) & 255 = 140 */
    return (result >> 16) & 255;
}
