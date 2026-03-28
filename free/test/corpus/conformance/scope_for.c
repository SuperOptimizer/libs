/* EXPECTED: 10 */
/* C89: loop variable declared before for, visible after loop */
int main(void) {
    int i;
    int sum = 0;
    for (i = 0; i < 4; i++) {
        sum += i; /* 0+1+2+3 = 6 */
    }
    /* i is still visible and equals 4 */
    return sum + i; /* 6 + 4 = 10 */
}
