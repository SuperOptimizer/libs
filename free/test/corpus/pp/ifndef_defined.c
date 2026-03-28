/* EXPECTED: 42 */
int main(void) {
    int x = 0;
#ifndef NEVER_DEFINED
    x = 42;
#endif
    return x;
}
