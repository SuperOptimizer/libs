/* EXPECTED: 60 */
/* array is automatically passed as pointer to first element */
int sum3(int *p) {
    return p[0] + p[1] + p[2];
}

int main(void) {
    int arr[3];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    return sum3(arr); /* arr decays to &arr[0] */
}
