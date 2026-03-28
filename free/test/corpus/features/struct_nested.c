/* EXPECTED: 42 */
struct inner {
    int val;
};
struct outer {
    struct inner in;
    int extra;
};
int main(void) {
    struct outer o;
    o.in.val = 40;
    o.extra = 2;
    return o.in.val + o.extra;
}
