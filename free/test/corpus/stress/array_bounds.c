/* EXPECTED: 99 */
int main(void) {
    int arr[100];
    int i;
    for (i = 0; i < 100; i++) {
        arr[i] = i;
    }
    return arr[99];
}
