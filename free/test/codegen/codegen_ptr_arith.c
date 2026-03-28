/* EXPECTED: 42 */
int main(void) {
    int arr[10];
    int *p;
    int *q;
    arr[0] = 10;
    arr[3] = 20;
    arr[7] = 12;
    p = arr;
    q = p + 3;
    return *p + *q + *(p + 7);
}
