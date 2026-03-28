/* EXPECTED: 0 */
int main(void) {
    int *p = nullptr;
    return p == nullptr ? 0 : 1;
}
