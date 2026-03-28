/* EXPECTED: 1 */
int main(void) {
    long x = 2147483647L;
    long y = x + 1L;
    return y == 2147483648L;
}
