/* EXPECTED: 1 */
/* unsigned vs signed comparison: -1 is converted to unsigned */
int main(void) {
    unsigned int u = 1;
    int s = -1;
    /* s is converted to unsigned, becoming UINT_MAX, which is > 1 */
    if (u < (unsigned int)s)
        return 1;
    return 0;
}
