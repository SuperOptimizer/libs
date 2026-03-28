/* EXPECTED: 4 */
struct S {
    int x;
    int y;
};
int main(void) {
    return __builtin_offsetof(struct S, y);
}
