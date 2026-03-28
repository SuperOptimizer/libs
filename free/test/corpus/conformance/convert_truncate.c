/* EXPECTED: 1 */
/* long to int truncation preserves low bits */
int main(void) {
    long big = 65537L; /* 0x10001 */
    int small;
    small = (int)big;
    /* On 32-bit int, 65537 fits. Verify value preserved. */
    if (small == 65537)
        return 1;
    return 0;
}
