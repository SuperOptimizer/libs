/* EXPECTED: 42 */
int main(void) {
    int x;
    int r;
    x = 3;
    r = 0;
    switch (x) {
        case 1:
            r = r + 1;
        case 2:
            r = r + 2;
            break;
        case 3:
            r = r + 10;
        case 4:
            r = r + 12;
        case 5:
            r = r + 20;
            break;
        default:
            r = 99;
    }
    return r;
}
