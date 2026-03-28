/* EXPECTED: 1 */
/* int op unsigned -> result is unsigned */
int main(void) {
    int s = -1;
    unsigned int u = 1;
    /* -1 + 1u: s converted to unsigned (UINT_MAX), UINT_MAX + 1 wraps to 0 */
    unsigned int result = s + u;
    if (result == 0)
        return 1;
    return 0;
}
