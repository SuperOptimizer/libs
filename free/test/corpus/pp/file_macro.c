/* EXPECTED: 1 */
int main(void) {
    const char *f = __FILE__;
    /* __FILE__ must be a non-empty string */
    if (f[0] != '\0') {
        return 1;
    }
    return 0;
}
