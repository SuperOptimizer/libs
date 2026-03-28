/* EXPECTED: 42 */
int classify(int a, int b) {
    switch (a) {
        case 1:
            switch (b) {
                case 1: return 11;
                case 2: return 12;
                default: return 10;
            }
        case 2:
            switch (b) {
                case 1: return 21;
                case 2: return 42;
                default: return 20;
            }
        default:
            return 0;
    }
}

int main(void) {
    return classify(2, 2);
}
