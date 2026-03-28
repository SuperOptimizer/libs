/* EXPECTED: 7 */
int result;
void set_result(int x) {
    result = x;
}
int main(void) {
    set_result(7);
    return result;
}
