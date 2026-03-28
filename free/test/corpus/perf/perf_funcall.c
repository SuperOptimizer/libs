/* EXPECTED: 128 */
/* 1 million function calls to test call overhead */
int add_one(int x) {
    return x + 1;
}

int add_two(int a, int b) {
    return a + b;
}

int main(void) {
    int i;
    int sum = 0;

    for (i = 0; i < 1000000; i++) {
        sum = add_one(sum);
        sum = add_two(sum, 1);
        /* Each iteration adds 2, but cap to prevent overflow */
        if (sum > 1000000000) {
            sum = sum & 0xFFFF;
        }
    }

    return sum & 0xFF;
}
