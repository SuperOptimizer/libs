/* EXPECTED: 42 */
int get_value(void) {
    return 42;
}
int main(void) {
    int (*fp)(void) = get_value;
    return fp();
}
