/* EXPECTED: 6 */
struct item {
    int val;
};
int main(void) {
    struct item items[3];
    int i, sum = 0;
    items[0].val = 1;
    items[1].val = 2;
    items[2].val = 3;
    for (i = 0; i < 3; i++)
        sum = sum + items[i].val;
    return sum;
}
