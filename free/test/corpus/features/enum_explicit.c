/* EXPECTED: 42 */
enum vals { A = 10, B = 20, C = 42 };
int main(void) {
    enum vals v = C;
    return v;
}
