/* EXPECTED: 42 */
#define FIRST(a, b) (a)
#define SECOND(a, b) (b)
int main(void) {
    /* comma inside parens is part of the sub-expression, not arg separator */
    return FIRST(SECOND(99, 42), 0);
}
