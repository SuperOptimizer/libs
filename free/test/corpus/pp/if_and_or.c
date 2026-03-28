/* EXPECTED: 42 */
#define A 1
#define B 1
int main(void) {
    int x = 0;
#if defined(A) && defined(B)
    x = 42;
#endif
#if defined(A) || defined(NOPE)
    /* this also true, but x already 42 */
#endif
#if defined(NOPE1) && defined(NOPE2)
    x = 99;
#endif
    return x;
}
