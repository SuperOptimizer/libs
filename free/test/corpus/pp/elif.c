/* EXPECTED: 42 */
#define MODE 2
int main(void) {
    int x = 0;
#if MODE == 1
    x = 10;
#elif MODE == 2
    x = 42;
#elif MODE == 3
    x = 99;
#else
    x = 0;
#endif
    return x;
}
