/* EXPECTED: 2 */
int main(void) {
    int x = 15;
    if (x > 20)
        return 1;
    else if (x > 10)
        return 2;
    else
        return 3;
}
