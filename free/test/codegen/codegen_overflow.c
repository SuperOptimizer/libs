/* EXPECTED: 42 */
int main(void) {
    unsigned char a;
    unsigned int b;
    a = 255;
    a = a + 1;  /* wraps to 0 */
    b = 4294967295u;
    b = b + 43; /* wraps to 42 */
    return (int)(a + b);
}
