/* EXPECTED: 5 */
int safe_div(int a, int b) {
    if (b != 0) {
        return a / b;
    }
    return 0;
}

int main(void) {
    int x = safe_div(10, 2);
    int y = safe_div(7, 0);
    return x + y;
}
