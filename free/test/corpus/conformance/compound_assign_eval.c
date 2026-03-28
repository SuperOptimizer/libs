/* EXPECTED: 7 */
/* compound assignment: a += b evaluates a's address once */
int main(void) {
    int arr[3];
    int i;
    arr[0] = 0;
    arr[1] = 5;
    arr[2] = 0;
    i = 1;
    arr[i] += 2; /* evaluates arr[i] once; arr[1] = 5 + 2 = 7 */
    return arr[1];
}
