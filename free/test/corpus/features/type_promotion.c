/* EXPECTED: 1 */
int main(void) {
    char a = 100;
    char b = 100;
    int r = a + b; /* promoted to int: 200 */
    return r == 200;
}
