/* EXPECTED: 0 */
/* static variables default to zero */
int main(void) {
    static int x;
    return x; /* must be 0 */
}
