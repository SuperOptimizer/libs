/* EXPECTED: 42 */
int main(void) {
    int a[3][3];
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            a[i][j] = i * 3 + j;
    /* a[2][2] = 8, but let's store and retrieve 42 */
    a[1][2] = 42;
    return a[1][2];
}
