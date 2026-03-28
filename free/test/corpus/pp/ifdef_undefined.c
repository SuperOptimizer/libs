/* EXPECTED: 42 */
int main(void) {
    int x = 42;
#ifdef NOT_DEFINED
    x = 99;
#endif
    return x;
}
