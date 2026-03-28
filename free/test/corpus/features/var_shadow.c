/* EXPECTED: 10 */
int main(void) {
    int x = 10;
    {
        int x = 99;
        (void)x;
    }
    return x;
}
