/* EXPECTED: 1 */
/* (void*)0 is a null pointer constant */
int main(void) {
    int *p = (void*)0;
    int *q = 0;
    if (p == 0 && q == (void*)0)
        return 1;
    return 0;
}
