/* EXPECTED: 1 */
union mixed {
    char c;
    int i;
    double d;
};
int main(void) {
    return sizeof(union mixed) == sizeof(double);
}
