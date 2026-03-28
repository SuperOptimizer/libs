/* EXPECTED: 42 */
int main(void) {
    int x = 42;
    if (x == 42)
        return x;
    __builtin_unreachable();
}
