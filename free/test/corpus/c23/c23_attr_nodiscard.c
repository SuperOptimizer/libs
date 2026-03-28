/* EXPECTED: 42 */
[[nodiscard]] int compute(void) {
    return 42;
}
int main(void) {
    int x = compute();
    return x;
}
