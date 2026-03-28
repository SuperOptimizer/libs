/* EXPECTED: 10 */
/* variable scope: inner block does not affect outer */
int main(void) {
    int x = 10;
    {
        int x = 99;
        (void)x;
    }
    return x; /* outer x is still 10 */
}
