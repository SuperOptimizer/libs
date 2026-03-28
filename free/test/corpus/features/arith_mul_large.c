/* EXPECTED: 1 */
int main(void) {
    long a = 100000L;
    long b = 100000L;
    long r = a * b;
    return r == 10000000000L;
}
