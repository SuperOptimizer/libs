/* EXPECTED: 55 */
/*
 * opt_push_pop.c - Expression with many temporaries.
 * A stack-based codegen (like gen.c) pushes/pops for every binary op.
 * Each push-pop pair that feeds the next instruction should be replaced
 * with a register mov by the peephole optimizer.
 */
int main(void)
{
    int a;
    int b;
    int c;
    int d;
    int e;
    int result;

    a = 1;
    b = 2;
    c = 3;
    d = 4;
    e = 5;

    /* lots of binary operations = lots of push/pop pairs */
    result = a + b + c + d + e + (a * b) + (c * d) + (d * e) + (a + e);
    /* 1+2+3+4+5 + 2 + 12 + 20 + 6 = 15+2+12+20+6 = 55 */
    return result;
}
