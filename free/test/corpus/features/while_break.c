/* EXPECTED: 5 */
int main(void) {
    int i = 0;
    while (i < 100) {
        if (i == 5)
            break;
        i = i + 1;
    }
    return i;
}
