/* EXPECTED: 54 */
/* Sieve of Eratosthenes up to 100000, count primes */
char sieve[100001];

int main(void) {
    int i, j;
    int count = 0;
    int limit = 100000;

    for (i = 0; i <= limit; i++) {
        sieve[i] = 1;
    }
    sieve[0] = 0;
    sieve[1] = 0;

    for (i = 2; i * i <= limit; i++) {
        if (sieve[i]) {
            for (j = i * i; j <= limit; j += i) {
                sieve[j] = 0;
            }
        }
    }

    for (i = 2; i <= limit; i++) {
        if (sieve[i]) count++;
    }
    /* There are 9592 primes below 100000 */
    /* 9592 & 0xFF = 104, 9592 >> 8 = 37, 104 ^ 37 = 77... */
    /* Just return count % 256 = 9592 % 256 = 9592 - 37*256 = 9592 - 9472 = 120 */
    /* Actually: 9592 & 0xFF = 0x2578 & 0xFF = 0x78 = 120 -- too big might be ok */
    /* Return count modulo a small prime for stable result */
    return count % 251;
}
