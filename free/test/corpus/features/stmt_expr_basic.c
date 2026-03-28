/* EXPECTED: 42 */
/* Basic GNU statement expression: ({ stmt; expr; }) evaluates to last expr */
int main(void) {
    int x;
    x = ({ int t = 40; t + 2; });
    return x;
}
