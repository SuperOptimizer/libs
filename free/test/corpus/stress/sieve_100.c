/* EXPECTED: 25 */
int main(void) {
    int sieve[100];
    int i, j, count;
    for (i = 0; i < 100; i++) {
        sieve[i] = 1;
    }
    sieve[0] = 0;
    sieve[1] = 0;
    for (i = 2; i < 10; i++) {
        if (sieve[i]) {
            for (j = i * i; j < 100; j = j + i) {
                sieve[j] = 0;
            }
        }
    }
    count = 0;
    for (i = 0; i < 100; i++) {
        if (sieve[i]) {
            count = count + 1;
        }
    }
    return count;
}
