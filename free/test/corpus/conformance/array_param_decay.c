/* EXPECTED: 1 */
/* array parameter decays to pointer */
int check(int *arr, int real_size) {
    /* arr is a pointer; sizeof(arr) is sizeof(int*), not the array */
    (void)arr;
    if (real_size > (int)sizeof(int *))
        return 1;
    return 0;
}

int main(void) {
    int a[10];
    /* sizeof(a) is 10*sizeof(int), which is > sizeof(int*) */
    return check(a, (int)sizeof(a));
}
