/* EXPECTED: 42 */
#define OUTER 1
#define INNER 1
int main(void) {
    int x = 0;
#if OUTER
#if INNER
    x = 42;
#else
    x = 10;
#endif
#else
    x = 99;
#endif
    return x;
}
