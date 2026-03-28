/* EXPECTED: 45 */
int main(void) {
    int n = 10;
    int arr[n];
    int i;
    for (i = 0; i < n; i++) {
        arr[i] = i;
    }
    int sum = 0;
    for (i = 0; i < n; i++) {
        sum += arr[i];
    }
    return sum;
}
