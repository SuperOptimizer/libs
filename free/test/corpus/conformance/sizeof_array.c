/* EXPECTED: 1 */
/* sizeof(array) gives total size, sizeof(pointer) gives pointer size */
int main(void) {
    int arr[10];
    int *p = arr;
    int arr_size = sizeof(arr);   /* 10 * sizeof(int) */
    int ptr_size = sizeof(p);     /* sizeof(int*) */
    if (arr_size > ptr_size)
        return 1;
    return 0;
}
