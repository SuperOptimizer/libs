/* EXPECTED: 42 */
int square(int x) {
    return x * x;
}

int main(void) {
    return square(6) + 6;
}
