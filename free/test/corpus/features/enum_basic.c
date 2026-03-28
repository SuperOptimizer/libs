/* EXPECTED: 3 */
enum color { RED, GREEN, BLUE, COUNT };
int main(void) {
    return COUNT; /* 0, 1, 2, 3 */
}
