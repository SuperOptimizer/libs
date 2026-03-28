/* EXPECTED: 20 */
int main(void) {
    int arr[1000];
    int i;
    long sum = 0;
    for (i = 0; i < 1000; i++) {
        arr[i] = i + 1;
    }
    for (i = 0; i < 1000; i++) {
        sum = sum + arr[i];
    }
    /* sum = 500500, last 8 bits = 500500 & 255 = 20 */
    return (int)(sum & 255);
}
