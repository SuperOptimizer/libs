/* EXPECTED: 3 */
/* static local is initialized only once, persists across calls */
int counter(void) {
    static int n = 0;
    n++;
    return n;
}

int main(void) {
    counter();
    counter();
    return counter(); /* 3rd call returns 3 */
}
