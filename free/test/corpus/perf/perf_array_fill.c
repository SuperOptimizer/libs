/* EXPECTED: 248 */
/* Fill and sum a 10000-element array */
int arr[10000];

int main(void) {
    int i;
    long sum = 0;

    for (i = 0; i < 10000; i++) {
        arr[i] = i * 7 + 3;
    }
    for (i = 0; i < 10000; i++) {
        sum += arr[i];
    }
    /* sum = sum(i*7+3, i=0..9999) = 7*sum(i)+3*10000
           = 7*(9999*10000/2) + 30000 = 349965000 + 30000 = 349995000 */
    return (int)(sum & 0xFF);
}
