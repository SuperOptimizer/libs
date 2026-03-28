/* EXPECTED: 42 */
#define EMPTY
int main(void) {
    int x;
    x = 42;
    EMPTY
    return x;
}
