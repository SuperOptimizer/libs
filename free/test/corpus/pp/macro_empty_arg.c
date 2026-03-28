/* EXPECTED: 42 */
#define PAIR(a, b) a
int main(void) {
    /* second argument is empty-ish: just a space, tests arg boundary parsing */
    return PAIR(42, );
}
