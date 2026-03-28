/* EXPECTED: 42 */
int counter(void) {
    static int n = 0;
    n = n + 7;
    return n;
}

int main(void) {
    int i;
    int r;
    r = 0;
    for (i = 0; i < 6; i++) {
        r = counter();
    }
    return r;
}
