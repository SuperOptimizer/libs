/* EXPECTED: 99 */
/* switch with no matching case and no default: body is skipped */
int main(void) {
    int x = 99;
    switch (5) {
        case 1: x = 1; break;
        case 2: x = 2; break;
        case 3: x = 3; break;
    }
    return x; /* no case matched, x unchanged */
}
