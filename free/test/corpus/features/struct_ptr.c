/* EXPECTED: 42 */
struct thing {
    int value;
};
int main(void) {
    struct thing t;
    struct thing *p = &t;
    p->value = 42;
    return t.value;
}
