/* EXPECTED: 6 */
/* nested struct/array initializer */
struct pair {
    int a;
    int b;
};

struct wrapper {
    struct pair p;
    int arr[2];
};

int main(void) {
    struct wrapper w = {{1, 2}, {3, 4}};
    /* w.p.a=1, w.p.b=2, w.arr[0]=3, w.arr[1]=4 */
    /* Verify a few fields: 1+2+3 = 6 */
    return w.p.a + w.p.b + w.arr[0];
}
