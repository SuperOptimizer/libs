/* EXPECTED: 0 */
int main(void) {
    _Alignas(16) int x = 100;
    return ((unsigned long)&x % 16 != 0);
}
