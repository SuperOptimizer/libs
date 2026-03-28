/* EXPECTED: 42 */
int main(void) {
    if (1) {
        if (1) return 42;
    }
    return 0;
}
