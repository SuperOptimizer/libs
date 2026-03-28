/* EXPECTED: 45 */
int main(void) {
    int i, s;
    s = 0;
    for (i = 0; i < 10; i = i + 1)
        s = s + i;
    return s;
}
