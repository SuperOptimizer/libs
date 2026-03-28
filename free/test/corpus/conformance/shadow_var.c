/* EXPECTED: 3 */
/* inner block shadows outer variable, outer restored after block */
int main(void) {
    int x = 1;
    {
        int x = 2;
        (void)x;
        {
            int x = 99;
            (void)x;
        }
        /* x is 2 here */
    }
    /* x is 1 here */
    return x + 2; /* 1 + 2 = 3 */
}
