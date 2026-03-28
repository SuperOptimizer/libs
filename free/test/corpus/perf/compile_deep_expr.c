/* EXPECTED: 0 */
int main(void) {
    int x = 1;
    int r;
    /* 80-deep expression tree stressing parser and codegen stack */
    r = (x + (x + (x + (x + (x + (x + (x + (x + (x + (x +
        (x + (x + (x + (x + (x + (x + (x + (x + (x + (x +
        (x + (x + (x + (x + (x + (x + (x + (x + (x + (x +
        (x + (x + (x + (x + (x + (x + (x + (x + (x + (x +
        (x + (x + (x + (x + (x + (x + (x + (x + (x + (x +
        (x + (x + (x + (x + (x + (x + (x + (x + (x + (x +
        (x + (x + (x + (x + (x + (x + (x + (x + (x + (x +
        (x + (x + (x + (x + (x + (x + (x + (x + (x + (x
        ))))))))))
        ))))))))))
        ))))))))))
        ))))))))))
        ))))))))))
        ))))))))))
        ))))))))))
        ))))))))))
        ;
    /* r should be 80 */
    return (r == 80) ? 0 : 1;
}
