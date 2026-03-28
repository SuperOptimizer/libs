/* EXPECTED: 1 */
int main(void) {
    short int a = 30000;
    short int b = 2767;
    short int c = a + b;
    return c == 32767;
}
