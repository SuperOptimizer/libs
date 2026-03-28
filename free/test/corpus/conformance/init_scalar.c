/* EXPECTED: 42 */
/* braced scalar initializer is valid C89 */
int main(void) {
    int x = {42};
    return x;
}
