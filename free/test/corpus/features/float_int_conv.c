/* EXPECTED: 42 */
int main(void) {
    int x = 42;
    double d = x;
    int y = (int)d;
    return y;
}
