/* EXPECTED: 42 */
#define FOO 1
int main(void) {
    int x = 0;
#if defined(FOO)
    x = 42;
#endif
    return x;
}
