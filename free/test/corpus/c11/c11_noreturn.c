/* EXPECTED: 42 */
void _exit(int status);
_Noreturn void die(void) {
    _exit(42);
}
int main(void) {
    int x = 42;
    if (x == 42)
        return x;
    die();
}
