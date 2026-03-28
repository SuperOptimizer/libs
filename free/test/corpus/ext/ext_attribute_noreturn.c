/* EXPECTED: 42 */
void _exit(int status);
__attribute__((noreturn)) void die(void) {
    _exit(1);
}
int main(void) {
    int x = 42;
    if (x == 42)
        return x;
    die();
}
