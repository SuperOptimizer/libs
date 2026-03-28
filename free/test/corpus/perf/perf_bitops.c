/* EXPECTED: 40 */
/* Millions of bitwise operations */
int main(void) {
    unsigned int a = 0xDEADBEEF;
    unsigned int b = 0xCAFEBABE;
    unsigned int c = 0x12345678;
    int i;

    for (i = 0; i < 5000000; i++) {
        a = (a ^ b) + (c >> 3);
        b = (b & c) | (a << 1);
        c = (c ^ a) + (b >> 2);
        a = a ^ (b & 0xFF00FF00);
        b = b + (c | 0x00FF00FF);
        c = (c << 3) ^ (a >> 5);
    }

    return (int)((a ^ b ^ c) & 0xFF);
}
