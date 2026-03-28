/* EXPECTED: 1 */
/* pointer comparison within same array is well-defined */
int main(void) {
    int arr[5];
    int *lo = &arr[1];
    int *hi = &arr[3];
    if (lo < hi && hi > lo && lo != hi)
        return 1;
    return 0;
}
