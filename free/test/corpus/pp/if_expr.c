/* EXPECTED: 42 */
int main(void) {
    int x = 0;
#if (2 + 3 > 4)
    x = 42;
#endif
    return x;
}
