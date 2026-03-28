/* EXPECTED: 2 */
int main(void) {
    int x = 5;
    return x > 10 ? 1 : x > 3 ? 2 : 3;
}
