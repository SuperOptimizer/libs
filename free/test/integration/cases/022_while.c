/* EXPECTED: 45 */
int main(void) {
    int i, s;
    i = 0;
    s = 0;
    while (i < 10) {
        s = s + i;
        i = i + 1;
    }
    return s;
}
