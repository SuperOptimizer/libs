/* EXPECTED: 1 */
/* int a[] = {1,2,3}; compiler deduces size 3 */
int main(void) {
    int a[] = {1, 2, 3};
    if (sizeof(a) == 3 * sizeof(int))
        return 1;
    return 0;
}
