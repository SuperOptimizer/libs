/* EXPECTED: 6 */
int main(void) {
    int x = 5;
    int y = ++x;
    return y; /* x is now 6, y gets 6 */
}
