/* EXPECTED: 1 */
int main(void) {
    int a = !0;   /* 1 */
    int b = !42;  /* 0 */
    return a == 1 && b == 0;
}
