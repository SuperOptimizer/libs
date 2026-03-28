/* EXPECTED: 42 */
/* extern declaration matches global definition */
int g = 42;

int get_g(void) {
    extern int g;
    return g;
}

int main(void) {
    return get_g();
}
