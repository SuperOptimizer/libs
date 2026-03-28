/* EXPECTED: 0 */
/* uninitialized global variables default to zero */
int g;
int arr[5];

int main(void) {
    return g + arr[0] + arr[4]; /* all zero */
}
