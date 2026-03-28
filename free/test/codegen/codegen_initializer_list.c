/* EXPECTED: 42 */
int main(void) {
    int arr[6] = {3, 5, 7, 9, 11, 7};
    int i;
    int sum;
    sum = 0;
    for (i = 0; i < 6; i++) {
        sum = sum + arr[i];
    }
    return sum;
}
