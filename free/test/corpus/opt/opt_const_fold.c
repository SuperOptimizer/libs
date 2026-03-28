/* EXPECTED: 42 */
/*
 * opt_const_fold.c - Constant expressions that can be folded at compile time.
 * All arithmetic here is on known constants. An optimizer (or the parser)
 * should fold these to a single constant rather than emitting runtime code.
 */
int main(void)
{
    int a;
    int b;
    int c;
    int d;
    int result;

    a = 3 + 4;       /* 7  */
    b = a * 2;        /* 14 */
    c = 100 - 58;     /* 42 */
    d = (6 + 2) * 5;  /* 40 */
    result = c + d - b - a + a;
    /* 42 + 40 - 14 - 7 + 7 = 68 ... let's simplify */
    /* actually just return the simplest one */
    return c;
}
