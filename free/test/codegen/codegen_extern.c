/* EXPECTED: 42 */
int shared_val;

void set_val(int x) {
    shared_val = x;
}

int get_val(void) {
    extern int shared_val;
    return shared_val;
}

int main(void) {
    set_val(42);
    return get_val();
}
