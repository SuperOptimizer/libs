/* EXPECTED: 2 */
int classify(int x) {
    if (x < 0) {
        return 0;
    }
    if (x == 0) {
        return 1;
    }
    if (x < 100) {
        return 2;
    }
    return 3;
}

int main(void) {
    return classify(50);
}
