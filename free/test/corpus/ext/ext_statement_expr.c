/* EXPECTED: 42 */
int main(void) {
    int r = ({
        int x = 40;
        x + 2;
    });
    return r;
}
