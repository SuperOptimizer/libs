/* EXPECTED: 42 */
int main(void) {
    int x;
    int y;
    x = 100000;   /* needs movz+movk, > 4095 */
    y = 99958;
    return x - y;
}
