/* EXPECTED: 7 */
/* basic bitfield: 3-bit field holds values 0-7 */
struct bits {
    unsigned int a : 3;
    unsigned int b : 5;
};

int main(void) {
    struct bits bf;
    bf.a = 7;  /* max for 3 bits */
    bf.b = 10;
    return bf.a; /* 7 */
}
