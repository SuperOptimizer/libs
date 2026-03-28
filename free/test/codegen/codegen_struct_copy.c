/* EXPECTED: 42 */
struct S {
    int a;
    int b;
    int c;
};

int main(void) {
    struct S x;
    struct S y;
    x.a = 10;
    x.b = 14;
    x.c = 18;
    y = x;
    return y.a + y.b + y.c;
}
