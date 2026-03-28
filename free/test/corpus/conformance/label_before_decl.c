/* EXPECTED: 5 */
/* C89: label must precede a statement, not a declaration */
/* Use a null statement (;) or expression after label */
int main(void) {
    int x = 0;
    goto skip;
    x = 99;
skip:
    ; /* null statement after label (C89 requires statement, not decl) */
    x = 5;
    return x;
}
