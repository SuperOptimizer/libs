/* EXPECTED: 251 */
int main(void) {
    int a = 3;
    int b = 8;
    int r = a - b; /* r == -5 */
    unsigned char c = (unsigned char)r; /* 256 - 5 = 251 */
    return (int)c;
}
