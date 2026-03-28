/* EXPECTED: 1 */
/* sizeof struct includes trailing padding for array alignment */
struct s {
    int i;
    char c;
};

int main(void) {
    /* struct must be padded to multiple of int alignment */
    if (sizeof(struct s) >= sizeof(int) + sizeof(char))
        return 1;
    return 0;
}
