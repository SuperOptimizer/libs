/* EXPECTED: 42 */
int main(void) {
    void *p = &&target;
    goto *p;
    return 0;
target:
    return 42;
}
