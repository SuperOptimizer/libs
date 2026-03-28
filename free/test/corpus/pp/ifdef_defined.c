/* EXPECTED: 42 */
#define HAVE_FEATURE
int main(void) {
    int x = 0;
#ifdef HAVE_FEATURE
    x = 42;
#endif
    return x;
}
