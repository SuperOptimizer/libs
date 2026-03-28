/* EXPECTED: 42 */
struct Pair {
    int a;
    int b;
};

struct Pair make_pair(int x, int y) {
    struct Pair p;
    p.a = x;
    p.b = y;
    return p;
}

int main(void) {
    struct Pair r;
    r = make_pair(19, 23);
    return r.a + r.b;
}
