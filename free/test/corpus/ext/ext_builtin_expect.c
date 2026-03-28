/* EXPECTED: 42 */
int main(void) {
    int x = 42;
    if (__builtin_expect(x == 42, 1)) {
        return x;
    }
    return 0;
}
