/* EXPECTED: 3 */
int counter(void) {
    static int n = 0;
    n++;
    return n;
}
int main(void) {
    counter();
    counter();
    return counter(); /* third call returns 3 */
}
