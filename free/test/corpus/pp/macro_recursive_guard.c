/* EXPECTED: 42 */
#define FOO FOO
int main(void) {
    /* FOO expands to FOO (itself) and stops; it becomes an identifier */
    int FOO = 42;
    return FOO;
}
