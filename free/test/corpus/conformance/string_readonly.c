/* EXPECTED: 72 */
/* string literal type is char[] in C89 (not const char[]) */
int main(void) {
    char *s = "Hello";
    return s[0]; /* 'H' == 72 */
}
