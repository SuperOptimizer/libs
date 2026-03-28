/* EXPECTED: 1 */
int main(void) {
    unsigned int x = __builtin_bswap32(0x01020304);
    return x & 0xFF;
}
