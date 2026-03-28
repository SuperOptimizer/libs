/* EXPECTED: 42 */
union data {
    int i;
    char c;
};
int main(void) {
    union data d;
    d.i = 42;
    return d.i;
}
