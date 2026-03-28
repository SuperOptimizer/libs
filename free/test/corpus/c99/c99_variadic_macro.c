/* EXPECTED: 0 */
#define SUM(a, ...) ((a) + sum_rest(__VA_ARGS__))
int sum_rest(int x, int y) {
    return x + y;
}
int main(void) {
    int r = SUM(10, 20, -30);
    return r;
}
