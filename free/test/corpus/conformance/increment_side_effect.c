/* EXPECTED: 10 */
/* post-increment: value used is the old value */
int main(void) {
    int a = 10;
    int b;
    b = a++;  /* b gets 10, a becomes 11 */
    if (a == 11 && b == 10)
        return 10;
    return 0;
}
