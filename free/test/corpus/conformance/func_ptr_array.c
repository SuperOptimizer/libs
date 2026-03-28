/* EXPECTED: 3 */
/* array of function pointers as dispatch table */
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }

int main(void) {
    int (*ops[3])(int, int);
    ops[0] = add;
    ops[1] = sub;
    ops[2] = mul;
    /* ops[0](1,2)=3, ops[1](5,3)=2, ops[2](2,3)=6 */
    return ops[0](1, 2); /* 3 */
}
