/* EXPECTED: 5 */
struct __attribute__((packed)) packed_s {
    char a;
    int b;
};
int main(void) {
    return sizeof(struct packed_s);
}
