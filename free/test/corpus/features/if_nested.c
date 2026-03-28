/* EXPECTED: 5 */
int main(void) {
    int x = 1;
    if (x) {
        if (x) {
            if (x) {
                if (x) {
                    if (x) {
                        return 5;
                    }
                }
            }
        }
    }
    return 0;
}
