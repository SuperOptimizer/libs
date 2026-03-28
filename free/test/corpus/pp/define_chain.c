/* EXPECTED: 42 */
#define A B
#define B 42
int main(void) {
    return A;
}
