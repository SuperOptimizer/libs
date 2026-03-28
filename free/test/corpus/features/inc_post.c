/* EXPECTED: 5 */
int main(void) {
    int x = 5;
    int y = x++;
    /* y gets old value 5, x is now 6 */
    return y;
}
