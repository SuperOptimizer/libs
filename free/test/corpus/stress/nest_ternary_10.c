/* EXPECTED: 10 */
int main(void) {
    int a = 1, b = 1, c = 1, d = 1, e = 1;
    int f = 1, g = 1, h = 1, i = 1, j = 0;
    return a ? (b ? (c ? (d ? (e ? (f ? (g ? (h ? (i ? (j ? 99 : 10)
        : 9) : 8) : 7) : 6) : 5) : 4) : 3) : 2) : 1;
}
