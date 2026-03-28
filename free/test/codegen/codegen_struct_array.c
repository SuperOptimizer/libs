/* EXPECTED: 42 */
struct Item {
    int val;
};

int main(void) {
    struct Item arr[5];
    int i;
    int sum;
    arr[0].val = 5;
    arr[1].val = 7;
    arr[2].val = 9;
    arr[3].val = 11;
    arr[4].val = 10;
    sum = 0;
    for (i = 0; i < 5; i++) {
        sum = sum + arr[i].val;
    }
    return sum;
}
