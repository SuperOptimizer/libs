/* EXPECTED: 42 */
struct E { int val; };
struct D { struct E *e; int pad; };
struct C { struct D *d; int pad; };
struct B { struct C *c; int pad; };
struct A { struct B *b; int pad; };

int main(void) {
    struct E e;
    struct D d;
    struct C c;
    struct B b;
    struct A a;
    e.val = 42;
    d.e = &e;
    c.d = &d;
    b.c = &c;
    a.b = &b;
    return a.b->c->d->e->val;
}
