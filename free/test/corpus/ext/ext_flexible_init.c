/* EXPECTED: 42 */
struct flex {
    int len;
    int data[];
};
int main(void) {
    char buf[sizeof(struct flex) + 2 * sizeof(int)];
    struct flex *f = (struct flex *)buf;
    f->len = 2;
    f->data[0] = 42;
    f->data[1] = 99;
    return f->data[0];
}
