/* EXPECTED: 55 */
int main(void) {
    int sum = 0;
    int i;
    for (i = 1; i <= 10; i++) {
        sum = sum + i;
    }
    return sum; /* 1+2+...+10 = 55 */
}
