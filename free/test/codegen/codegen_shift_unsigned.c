/* EXPECTED: 42 */
int main(void) {
    unsigned int x;
    unsigned int y;
    x = 0xAC000000u;  /* high bit set, must use logical shift right */
    y = x >> 26;       /* logical shift: 0xAC >> 2 = 0x2B = 43... let's recalc */
    /* 0xAC000000 = 2885681152; >> 26 = 2885681152 / 67108864 = 43 (0x2B) */
    /* We want 42, so adjust: */
    x = 0xA8000000u;  /* 0xA8 >> 2 = 0x2A = 42 */
    y = x >> 26;
    return (int)y;
}
