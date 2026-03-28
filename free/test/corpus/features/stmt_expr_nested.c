/* EXPECTED: 15 */
/* Nested statement expressions */
int main(void) {
    int r;
    r = ({
        int a = 5;
        int b = ({
            int c = 10;
            c;
        });
        a + b;
    });
    return r;
}
