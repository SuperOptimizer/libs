/* EXPECTED: 42 */
int foo(void) {
    return 42;
    return 99;
}

int main(void) {
    return foo();
}
