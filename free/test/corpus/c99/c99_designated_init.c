/* EXPECTED: 42 */
int main(void) {
    int a[5] = {[3] = 42};
    return a[3];
}
