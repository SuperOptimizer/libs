/* EXPECTED: 0 */
struct header {
    int len;
    int data[0];
};
int main(void) {
    return sizeof(struct header) - sizeof(int);
}
