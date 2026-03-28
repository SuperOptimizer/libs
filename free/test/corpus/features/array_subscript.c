/* EXPECTED: 1 */
int main(void) {
    int a[] = {10, 20, 30, 40, 50};
    int i = 3;
    return a[i] == *(a + i); /* both are 40 */
}
