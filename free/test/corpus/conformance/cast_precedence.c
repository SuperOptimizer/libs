/* EXPECTED: 1 */
/* cast binds tighter than addition: (int)a + b != (int)(a + b) in general */
int main(void) {
    char a = 10;
    char b = 20;
    int r1 = (int)a + b;    /* cast a to int, then add b (promoted) = 30 */
    int r2 = (int)(a + b);  /* add a+b (promoted to int), then cast = 30 */
    /* both are 30 here, but the parse trees differ */
    if (r1 == 30 && r2 == 30)
        return 1;
    return 0;
}
