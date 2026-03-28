/* EXPECTED: 42 */
struct variant {
    int tag;
    union {
        int ival;
        float fval;
    };
};
int main(void) {
    struct variant v;
    v.tag = 1;
    v.ival = 42;
    return v.ival;
}
