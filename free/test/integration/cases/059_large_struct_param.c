/* EXPECTED: 42 */
/* Test: large struct (>16 bytes) passed by reference per AAPCS64 */
struct Big { int a; int b; int c; int d; int e; };

int sum(struct Big s) {
    return s.a + s.b + s.c + s.d + s.e;
}

int main(void) {
    struct Big b;
    b.a = 5;
    b.b = 10;
    b.c = 7;
    b.d = 8;
    b.e = 12;
    return sum(b);
}
