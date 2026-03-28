/* EXPECTED: 3 */
/* break exits only the innermost loop */
int main(void) {
    int count = 0;
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 100; j++) {
            break; /* exits inner loop only */
        }
        count++; /* still runs for each outer iteration */
    }
    return count; /* 3 */
}
