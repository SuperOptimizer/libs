/* EXPECTED: 1 */
int main(void) {
    int x = 10;
    x += 5;  /* 15 */
    x -= 3;  /* 12 */
    x *= 2;  /* 24 */
    x /= 4;  /* 6 */
    x %= 5;  /* 1 */
    return x;
}
