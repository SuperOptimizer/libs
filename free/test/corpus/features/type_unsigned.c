/* EXPECTED: 1 */
int main(void) {
    unsigned int x = 0;
    unsigned int y = x - 1; /* wraps to UINT_MAX */
    return y > 0;
}
