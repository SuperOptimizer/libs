/* EXPECTED: 42 */
int main(void) {
    int x = 0;
    int *p = &x;
    *p = 42;
    return x;
}
