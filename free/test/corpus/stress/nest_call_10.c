/* EXPECTED: 10 */
int f1(int x) { return x + 1; }
int f2(int x) { return x + 1; }
int f3(int x) { return x + 1; }
int f4(int x) { return x + 1; }
int f5(int x) { return x + 1; }
int f6(int x) { return x + 1; }
int f7(int x) { return x + 1; }
int f8(int x) { return x + 1; }
int f9(int x) { return x + 1; }
int f10(int x) { return x + 1; }

int main(void) {
    return f1(f2(f3(f4(f5(f6(f7(f8(f9(f10(0))))))))));
}
