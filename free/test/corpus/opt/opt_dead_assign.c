/* EXPECTED: 20 */
/*
 * opt_dead_assign.c - Variable assigned but overwritten before use.
 * The first assignment to x is dead: it is overwritten before any read.
 * An optimizer should eliminate the dead store.
 */
int main(void)
{
    int x;
    int y;

    x = 5;    /* dead store: x overwritten below before use */
    x = 10;
    y = x + 10;
    return y;
}
