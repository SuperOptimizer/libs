/* EXPECTED: 255 */
/* Copy 1MB of data byte-by-byte in a loop */
char src[1024];
char dst[1024];

int main(void) {
    int round, i;
    int checksum = 0;

    /* Fill source buffer */
    for (i = 0; i < 1024; i++) {
        src[i] = (char)(i & 0xFF);
    }

    /* Copy 1024 bytes, 1024 times = 1MB total copied */
    for (round = 0; round < 1024; round++) {
        for (i = 0; i < 1024; i++) {
            dst[i] = src[i];
        }
        /* Perturb source slightly to prevent over-optimization */
        src[round & 0x3FF] = (char)(src[round & 0x3FF] + 1);
    }

    /* Checksum from dst */
    for (i = 0; i < 1024; i++) {
        checksum = (checksum + dst[i]) & 0xFFFF;
    }
    return checksum & 0xFF;
}
