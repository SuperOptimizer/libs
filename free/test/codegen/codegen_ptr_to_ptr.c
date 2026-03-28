/* EXPECTED: 42 */
int main(void) {
    int x;
    int *p;
    int **pp;
    x = 42;
    p = &x;
    pp = &p;
    return **pp;
}
