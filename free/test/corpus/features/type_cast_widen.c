/* EXPECTED: 1 */
int main(void) {
    signed char c = -5;
    int x = (int)c; /* sign-extends to -5 */
    return x == -5;
}
