/* EXPECTED: 42 */
void copy(int * restrict dst, const int * restrict src, int n) {
    int i;
    for (i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}
int main(void) {
    int a[1] = {42};
    int b[1];
    copy(b, a, 1);
    return b[0];
}
