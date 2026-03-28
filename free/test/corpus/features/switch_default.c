/* EXPECTED: 99 */
int main(void) {
    int x = 42;
    switch (x) {
        case 1: return 1;
        case 2: return 2;
        default: return 99;
    }
}
