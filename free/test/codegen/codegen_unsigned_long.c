/* EXPECTED: 42 */
int main(void) {
    unsigned long a;
    unsigned long b;
    unsigned long mask;
    a = 0xFF;
    b = 0xD5;
    /* a & b = 0xD5 = 213; 213 % 256 is 213; let's do simpler */
    mask = (1UL << 8) - 1;  /* 0xFF */
    a = 170UL;  /* 0xAA */
    b = a & mask;
    /* b = 170, 170 % 128 = 42 */
    return (int)(b % 128);
}
