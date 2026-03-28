/* EXPECTED: 10 */
/* array name decays to pointer to first element */
int main(void) {
    int arr[3];
    int *p;
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    p = arr; /* arr decays to &arr[0] */
    return *p; /* 10 */
}
