/* EXPECTED: 42 */
int main(void) {
    int x = 42;
    __typeof__(x) y = x;
    return y;
}
