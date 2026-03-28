/* EXPECTED: 42 */
struct outer {
    int a;
    struct {
        int b;
        int c;
    };
};
int main(void) {
    struct outer o;
    o.a = 1;
    o.b = 42;
    o.c = 3;
    return o.b;
}
