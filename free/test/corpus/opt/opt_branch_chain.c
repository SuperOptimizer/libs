/* EXPECTED: 3 */
/*
 * opt_branch_chain.c - Nested if/else creating branch chains.
 * Each if/else emits a forward branch + label. When multiple conditions
 * are chained, the codegen produces sequences like:
 *     b .L1
 *     .L1:
 * which can be eliminated (branch to immediately-next instruction).
 */
int main(void)
{
    int x;
    int r;

    x = 15;
    r = 0;

    if (x > 20) {
        r = 1;
    } else if (x > 10) {
        if (x > 14) {
            r = 3;
        } else {
            r = 2;
        }
    } else if (x > 5) {
        r = 4;
    } else {
        r = 5;
    }

    return r;
}
