/* EXPECTED: 4 */
int main(void) {
    int x = 5;
    int y = --x;
    return y; /* x is now 4, y gets 4 */
}
