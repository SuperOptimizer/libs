/* EXPECTED: 42 */
/* assignment expression returns the assigned value */
int main(void) {
    int x, y;
    x = (y = 42);
    /* y is 42, x gets the result of (y = 42) which is 42 */
    if (x == 42 && y == 42)
        return 42;
    return 0;
}
