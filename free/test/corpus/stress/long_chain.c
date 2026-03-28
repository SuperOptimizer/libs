/* EXPECTED: 42 */
struct E { int val; };
struct D { struct E e; };
struct C { struct D d; };
struct B { struct C c; };
struct A { struct B b; };

int main(void) {
    struct A a;
    a.b.c.d.e.val = 42;
    return a.b.c.d.e.val;
}
