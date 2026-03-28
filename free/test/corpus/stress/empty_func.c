/* EXPECTED: 0 */
void do_nothing(void) {
}

int also_nothing(void) {
    return 0;
}

int main(void) {
    do_nothing();
    return also_nothing();
}
