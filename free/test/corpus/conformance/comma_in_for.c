/* EXPECTED: 15 */
/* comma operator in for loop: both expressions are evaluated */
int main(void) {
    int i, sum;
    sum = 0;
    for (i = 1; i <= 5; i++, sum += i - 1) {
        /* sum accumulates: 0+1+2+3+4+5 = 15 */
    }
    return sum;
}
