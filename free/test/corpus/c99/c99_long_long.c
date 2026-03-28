/* EXPECTED: 0 */
int main(void) {
    long long x = 1LL << 40;
    return (int)(x & 0xFF);
}
