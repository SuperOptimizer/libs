/* EXPECTED: 42 */
/* Test basic bitfield support: packing, load, store */

struct flags {
    unsigned int type : 4;
    unsigned int size : 28;
};

int main(void) {
    struct flags f;
    f.type = 10;
    f.size = 32;
    return f.type + f.size;
}
