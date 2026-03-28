/* EXPECTED: 42 */
static inline int add(int a, int b) {
    return a + b;
}
int main(void) {
    return add(40, 2);
}
