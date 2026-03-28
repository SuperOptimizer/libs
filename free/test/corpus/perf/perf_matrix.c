/* EXPECTED: 5 */
/* Multiply two 50x50 integer matrices */
int a[50][50];
int b[50][50];
int c[50][50];

int main(void) {
    int i, j, k;
    int checksum = 0;

    /* Initialize matrices */
    for (i = 0; i < 50; i++) {
        for (j = 0; j < 50; j++) {
            a[i][j] = (i + j) % 7;
            b[i][j] = (i * 2 + j) % 5;
        }
    }

    /* Matrix multiply c = a * b */
    for (i = 0; i < 50; i++) {
        for (j = 0; j < 50; j++) {
            c[i][j] = 0;
            for (k = 0; k < 50; k++) {
                c[i][j] += a[i][k] * b[k][j];
            }
        }
    }

    /* Compute checksum from result */
    for (i = 0; i < 50; i++) {
        for (j = 0; j < 50; j++) {
            checksum += c[i][j];
        }
    }
    /* checksum varies but return low bits */
    return (checksum & 0x7F) | 1;
}
