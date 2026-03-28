/* EXPECTED: 1 */
int main(void) {
    int x = 300;
    char c = (char)x; /* truncates to 300 - 256 = 44 */
    return c == 44;
}
