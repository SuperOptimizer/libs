/* EXPECTED: 0 */
int flag = 0;
int side_effect(void) {
    flag = 1;
    return 1;
}
int main(void) {
    int r = 0 && side_effect();
    /* flag must still be 0: side_effect was not called */
    return flag;
}
