/* EXPECTED: 6 */
/* all cases fall through: execution continues into subsequent cases */
int main(void) {
    int sum = 0;
    switch (1) {
        case 1: sum += 1; /* fall through */
        case 2: sum += 2; /* fall through */
        case 3: sum += 3; /* fall through */
    }
    return sum; /* 1 + 2 + 3 = 6 */
}
