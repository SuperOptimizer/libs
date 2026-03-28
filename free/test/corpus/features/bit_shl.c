/* EXPECTED: 1 */
int main(void) {
    int x = 1 << 10;
    return x == 1024;
}
