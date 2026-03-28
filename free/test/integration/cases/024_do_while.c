/* EXPECTED: 42 */
int main(void) {
    int i;
    i = 0;
    do {
        i = i + 1;
    } while (i < 42);
    return i;
}
