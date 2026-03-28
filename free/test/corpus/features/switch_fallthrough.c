/* EXPECTED: 6 */
int main(void) {
    int x = 1;
    int r = 0;
    switch (x) {
        case 1: r = r + 1; /* fall through */
        case 2: r = r + 2; /* fall through */
        case 3: r = r + 3; break;
        case 4: r = r + 4; break;
    }
    return r; /* 1 + 2 + 3 = 6 */
}
