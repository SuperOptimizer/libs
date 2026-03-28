/* EXPECTED: 42 */
/* Test: nested struct member access, mixed arrow/dot, struct copy */
struct Inner { int a; int b; };
struct Outer { struct Inner inner; int c; };

int main(void) {
    struct Outer o, copy;
    struct Outer *p;
    o.inner.a = 10;
    o.inner.b = 20;
    o.c = 12;
    p = &o;
    copy = o;
    return p->inner.a + copy.inner.b + o.c;
}
