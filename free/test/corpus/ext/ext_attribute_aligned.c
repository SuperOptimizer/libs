/* EXPECTED: 0 */
int main(void) {
    __attribute__((aligned(16))) int x = 100;
    return ((unsigned long)&x % 16 != 0);
}
