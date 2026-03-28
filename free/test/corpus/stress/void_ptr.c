/* EXPECTED: 42 */
int main(void) {
    int x = 42;
    void *vp = (void *)&x;
    int *ip = (int *)vp;
    return *ip;
}
