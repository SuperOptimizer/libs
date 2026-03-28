/* EXPECTED: 42 */
struct S {
    int x;
    int y;
};
int main(void) {
    struct S s = {.y = 42, .x = 1};
    return s.y;
}
