/* EXPECTED: 1 */
int main(void) {
    int x = -16;
    int r = x >> 2; /* arithmetic shift: -4 */
    return r == -4;
}
