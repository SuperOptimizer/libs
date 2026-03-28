/* EXPECTED: 42 */
int main(void) {
    int x;
    __asm__("mov %w0, #42" : "=r"(x));
    return x;
}
