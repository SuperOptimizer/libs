/* EXPECTED: 1 */
int main(void) {
    int x = 3;
    switch (x) {
        case 1 ... 5:
            return 1;
        case 6 ... 10:
            return 2;
        default:
            return 0;
    }
}
