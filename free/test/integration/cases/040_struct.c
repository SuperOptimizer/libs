/* EXPECTED: 42 */
struct S {
    int x;
    int y;
};

int main(void) {
    struct S s;
    s.x = 20;
    s.y = 22;
    return s.x + s.y;
}
