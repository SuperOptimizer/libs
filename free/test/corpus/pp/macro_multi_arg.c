/* EXPECTED: 42 */
#define SUM5(a, b, c, d, e) ((a) + (b) + (c) + (d) + (e))
int main(void) {
    return SUM5(2, 4, 8, 12, 16);
}
