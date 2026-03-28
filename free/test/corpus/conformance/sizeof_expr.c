/* EXPECTED: 5 */
/* sizeof does not evaluate its operand */
int main(void) {
    int a = 5;
    int s;
    s = sizeof(a++); /* a++ is NOT evaluated */
    (void)s;
    return a; /* a is still 5, not 6 */
}
