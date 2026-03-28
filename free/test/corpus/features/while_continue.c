/* EXPECTED: 25 */
int main(void) {
    int i = 0;
    int sum = 0;
    while (i < 10) {
        i = i + 1;
        if (i % 2 == 0)
            continue;
        sum = sum + i; /* 1+3+5+7+9 = 25 */
    }
    return sum;
}
