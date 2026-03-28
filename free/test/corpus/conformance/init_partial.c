/* EXPECTED: 1 */
/* partial initializer: remaining elements are zero */
int main(void) {
    int a[5] = {1, 2};
    /* a[0]=1, a[1]=2, a[2]=0, a[3]=0, a[4]=0 */
    if (a[0] == 1 && a[1] == 2 && a[2] == 0 && a[3] == 0 && a[4] == 0)
        return 1;
    return 0;
}
