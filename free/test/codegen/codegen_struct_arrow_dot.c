/* EXPECTED: 42 */
struct Inner {
    int field;
};

struct Outer {
    struct Inner member;
    int other;
};

int main(void) {
    struct Outer s;
    struct Outer *p;
    s.member.field = 30;
    s.other = 12;
    p = &s;
    return p->member.field + p->other;
}
