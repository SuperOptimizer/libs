/* EXPECTED: 20 */
int main(void) {
    int arr[5];
    int *p = arr;
    int *q = p + 1;
    /* difference in bytes should be sizeof(int) */
    return (int)((char *)q - (char *)p) * 5; /* 4 * 5 = 20 */
}
