/* EXPECTED: 42 */
struct Pair {
    int a;
    int b;
};

int sum_pair(struct Pair p) {
    return p.a + p.b;
}

int main(void) {
    struct Pair p;
    p.a = 19;
    p.b = 23;
    return sum_pair(p);
}
