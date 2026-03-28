/* EXPECTED: 1 */
int main(void) {
    int r = -7 % 3;
    /* C89: implementation-defined, but common result is -1 */
    return r == -1;
}
