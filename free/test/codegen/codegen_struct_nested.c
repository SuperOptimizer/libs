/* EXPECTED: 42 */
struct Inner {
    int x;
    int y;
};

struct Outer {
    int a;
    struct Inner inner;
};

int main(void) {
    struct Outer s;
    s.a = 10;
    s.inner.x = 15;
    s.inner.y = 17;
    return s.a + s.inner.x + s.inner.y;
}
