/* EXPECTED: 30 */
int main(void) {
    int a[3][3];
    int b[3][3];
    int c[3][3];
    int i, j, k;
    /* Identity-like matrix a */
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            a[i][j] = (i == j) ? 2 : 1;
    /* Matrix b = {{1,2,3},{4,5,6},{7,8,9}} */
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            b[i][j] = i * 3 + j + 1;
    /* Multiply c = a * b */
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            c[i][j] = 0;
            for (k = 0; k < 3; k++)
                c[i][j] = c[i][j] + a[i][k] * b[k][j];
        }
    /* c[0][0] = 2*1+1*4+1*7 = 13, c[0][1] = 2*2+1*5+1*8 = 17 */
    /* Return c[0][0] + c[0][1] = 13 + 17 = 30 */
    return c[0][0] + c[0][1];
}
