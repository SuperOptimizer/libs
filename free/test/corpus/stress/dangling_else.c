/* EXPECTED: 3 */
int main(void) {
    int a = 1, b = 0;
    int x = 0;
    /* The else binds to the inner if */
    if (a)
        if (b)
            x = 1;
        else
            x = 3;
    return x;
}
