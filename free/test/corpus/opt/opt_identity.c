/* EXPECTED: 42 */
/*
 * opt_identity.c - Identity operations: add 0, multiply by 1, shift by 0.
 * All of these operations produce no change in value and generate
 * instructions like "add x0, x0, #0" which the peephole optimizer
 * should eliminate entirely.
 */
int main(void)
{
    int x;
    int y;
    int z;
    int w;

    x = 42;
    x = x + 0;    /* add 0: identity */
    x = x - 0;    /* sub 0: identity */
    y = x * 1;    /* mul 1: identity */
    z = y << 0;   /* shift left by 0: identity */
    w = z >> 0;   /* shift right by 0: identity */

    return w;
}
