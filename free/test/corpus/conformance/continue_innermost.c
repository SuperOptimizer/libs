/* EXPECTED: 3 */
/* continue affects only the innermost loop */
int main(void) {
    int outer_count = 0;
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 5; j++) {
            continue; /* skips rest of inner body */
        }
        outer_count++; /* still executes */
    }
    return outer_count; /* 3 */
}
