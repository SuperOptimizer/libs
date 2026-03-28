/* EXPECTED: 0 */
int main(void) {
    unsigned int x = 0;
    x = x - 1; /* wraps to UINT_MAX */
    x = x + 1; /* wraps back to 0 */
    return (int)x;
}
