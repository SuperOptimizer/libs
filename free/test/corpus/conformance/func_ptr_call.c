/* EXPECTED: 25 */
/* call through function pointer */
int square(int x) {
    return x * x;
}

int main(void) {
    int (*fp)(int);
    fp = square;
    return fp(5); /* 25 */
}
