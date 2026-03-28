/* EXPECTED: 10 */
int sum(int *p, int n) {
    int i, s = 0;
    for (i = 0; i < n; i++)
        s = s + p[i];
    return s;
}
int main(void) {
    int a[] = {1, 2, 3, 4};
    return sum(a, 4); /* array decays to pointer */
}
