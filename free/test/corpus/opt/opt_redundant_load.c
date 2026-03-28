/* EXPECTED: 20 */
/*
 * opt_redundant_load.c - Variable loaded twice without intervening store.
 * After storing to x, a naive codegen will reload x from the stack
 * even though the value is still in a register. The optimizer should
 * eliminate the redundant load.
 */
int main(void)
{
    int x;
    int y;
    int z;

    x = 10;
    y = x;        /* first use of x: may generate load */
    z = x + y;    /* second use of x: redundant load if x still in reg */
    return z;
}
