/* EXPECTED: 42 */
int main(void) {
    int arr[5];
    int *p;
    int i;
    int sum;
    arr[0] = 5;
    arr[1] = 7;
    arr[2] = 9;
    arr[3] = 11;
    arr[4] = 10;
    p = &arr[0];
    sum = 0;
    for (i = 0; i < 5; i++) {
        sum = sum + *(p + i);
    }
    return sum;
}
