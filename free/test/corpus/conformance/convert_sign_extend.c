/* EXPECTED: 1 */
/* signed char to int sign-extends */
int main(void) {
    signed char c = -5;
    int i = c; /* sign-extended: i should be -5 */
    if (i == -5)
        return 1;
    return 0;
}
