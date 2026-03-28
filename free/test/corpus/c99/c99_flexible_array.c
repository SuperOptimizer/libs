/* EXPECTED: 42 */
struct flex {
    int len;
    int data[];
};
int main(void) {
    char buf[sizeof(struct flex) + 4 * sizeof(int)];
    struct flex *f = (struct flex *)buf;
    f->len = 4;
    f->data[0] = 42;
    return f->data[0];
}
