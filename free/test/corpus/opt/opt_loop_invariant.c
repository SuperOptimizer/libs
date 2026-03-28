/* EXPECTED: 100 */
/*
 * opt_loop_invariant.c - Computation that could be hoisted out of loop.
 * The expression (a + b) does not change within the loop, so it could
 * be computed once before the loop. This is a future optimization target.
 * For now we verify correctness is preserved.
 */
int main(void)
{
    int a;
    int b;
    int sum;
    int i;

    a = 7;
    b = 3;
    sum = 0;

    for (i = 0; i < 10; i = i + 1) {
        sum = sum + (a + b);  /* a+b = 10 each iteration */
    }

    return sum;
}
