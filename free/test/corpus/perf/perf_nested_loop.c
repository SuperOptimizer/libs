/* EXPECTED: 32 */
/* Triple nested loop: 100^3 = 1,000,000 iterations */
int main(void) {
    int i, j, k;
    int sum = 0;

    for (i = 0; i < 100; i++) {
        for (j = 0; j < 100; j++) {
            for (k = 0; k < 100; k++) {
                sum += (i ^ j ^ k) & 1;
            }
        }
    }
    /* sum counts how many (i,j,k) triples have odd xor */
    return sum & 0xFF;
}
