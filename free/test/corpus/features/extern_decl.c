/* EXPECTED: 99 */
int shared = 99;

int get_shared(void);

int get_shared(void) {
    extern int shared;
    return shared;
}

int main(void) {
    return get_shared();
}
