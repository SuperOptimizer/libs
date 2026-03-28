/* EXPECTED: 64 */
/* Copy a large struct 100000 times */
struct big {
    int data[16];
};

int main(void) {
    struct big a;
    struct big b;
    int i, round;
    int checksum = 0;

    /* Initialize */
    for (i = 0; i < 16; i++) {
        a.data[i] = i * 3 + 7;
    }

    /* Copy 100000 times, accumulate to prevent dead code elimination */
    for (round = 0; round < 100000; round++) {
        b = a;
        /* Perturb a slightly */
        a.data[round & 0xF] += 1;
        checksum += b.data[round & 0xF];
    }

    return checksum & 0xFF;
}
