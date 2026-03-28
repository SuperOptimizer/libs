/* EXPECTED: 1 */
int main(void) {
    int count = 0;
    int x = 0;
    do {
        count++;
    } while (x != 0);
    return count; /* executes exactly once */
}
