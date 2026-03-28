/* EXPECTED: 42 */
int main(void) {
    int x = 0;
#if 1
    x = 42;
#endif
    return x;
}
