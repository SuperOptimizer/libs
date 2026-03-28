/* EXPECTED: 7 */
int main(void) {
    int x = 7;
    x = x;
    x = x;
    x = x;
    return x;
}
