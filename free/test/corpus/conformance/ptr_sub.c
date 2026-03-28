/* EXPECTED: 3 */
/* pointer subtraction yields element count between them */
int main(void) {
    int arr[5];
    int *p = &arr[1];
    int *q = &arr[4];
    int diff = (int)(q - p); /* 4 - 1 = 3 elements */
    return diff;
}
