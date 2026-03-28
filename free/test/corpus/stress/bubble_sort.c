/* EXPECTED: 1 */
int main(void) {
    int arr[20];
    int i, j, tmp;
    /* fill in reverse order */
    for (i = 0; i < 20; i++) {
        arr[i] = 20 - i;
    }
    /* bubble sort */
    for (i = 0; i < 19; i++) {
        for (j = 0; j < 19 - i; j++) {
            if (arr[j] > arr[j+1]) {
                tmp = arr[j];
                arr[j] = arr[j+1];
                arr[j+1] = tmp;
            }
        }
    }
    /* first sorted element should be 1 */
    return arr[0];
}
