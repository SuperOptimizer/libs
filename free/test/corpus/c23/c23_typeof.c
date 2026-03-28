/* EXPECTED: 42 */
int main(void) {
    int x = 42;
    typeof(x) y = x;
    return y;
}
