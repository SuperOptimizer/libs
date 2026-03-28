/* EXPECTED: 42 */
int main(void) {
    int x = 42;
#if 0
    x = 99;
#endif
    return x;
}
