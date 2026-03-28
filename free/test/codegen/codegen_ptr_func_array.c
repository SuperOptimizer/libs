/* EXPECTED: 42 */
int add10(int x) { return x + 10; }
int add20(int x) { return x + 20; }
int add30(int x) { return x + 30; }

int main(void) {
    int (*funcs[3])(int);
    funcs[0] = add10;
    funcs[1] = add20;
    funcs[2] = add30;
    /* add20(22) = 42 */
    return funcs[1](22);
}
