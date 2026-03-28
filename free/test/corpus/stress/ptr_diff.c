/* EXPECTED: 5 */
int main(void) {
    int arr[10];
    int *p = &arr[2];
    int *q = &arr[7];
    int diff = (int)(q - p);
    return diff;
}
