/* EXPECTED: 200 */
/* char operands are promoted to int before addition */
int main(void) {
    char a = 100;
    char b = 100;
    int c = a + b; /* promoted to int: 100+100=200, no overflow */
    return c;
}
