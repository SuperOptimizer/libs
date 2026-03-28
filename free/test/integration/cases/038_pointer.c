/* EXPECTED: 42 */
int main(void) {
    int x;
    int *p;
    x = 42;
    p = &x;
    return *p;
}
