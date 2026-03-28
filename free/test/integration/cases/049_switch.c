/* EXPECTED: 42 */
int main(void) {
    int x;
    x = 2;
    switch (x) {
        case 1: return 1;
        case 2: return 42;
        default: return 0;
    }
}
