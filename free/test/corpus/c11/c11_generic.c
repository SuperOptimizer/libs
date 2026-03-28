/* EXPECTED: 1 */
int main(void) {
    int x = 0;
    return _Generic(x, int: 1, long: 2, default: 0);
}
