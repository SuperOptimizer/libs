/* EXPECTED: 1 */
/* union: write one member, read another (type punning) */
union pun {
    int i;
    char c[4];
};

int main(void) {
    union pun u;
    u.i = 0;
    u.c[0] = 1; /* set lowest byte to 1 */
    /* on little-endian, u.i should have bit 0 set */
    if (u.i != 0)
        return 1;
    return 0;
}
