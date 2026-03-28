/* EXPECTED: 4 */
int main(void) {
    int x;
    x = __LINE__;
    /* __LINE__ on line 4, so x == 4 */
    return x;
}
