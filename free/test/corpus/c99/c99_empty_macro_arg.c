/* EXPECTED: 42 */
#define PICK(a, b, c) c
int main(void) {
    return PICK(, , 42);
}
