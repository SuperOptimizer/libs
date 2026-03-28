/* EXPECTED: 2 */
int main(void) {
    int x = 2;
    switch (x) {
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
    }
    return 0;
}
