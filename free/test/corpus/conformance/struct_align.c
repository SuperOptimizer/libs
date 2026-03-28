/* EXPECTED: 1 */
/* struct padding: members aligned to their natural alignment */
struct padded {
    char c;
    int i;
};

int main(void) {
    /* sizeof(struct padded) > sizeof(char) + sizeof(int) due to padding */
    if (sizeof(struct padded) > 5)
        return 1;
    return 0;
}
