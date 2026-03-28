/* EXPECTED: 7 */
int main(void) {
    int i = 0;
    for (;;) {
        if (i == 7)
            break;
        i = i + 1;
    }
    return i;
}
