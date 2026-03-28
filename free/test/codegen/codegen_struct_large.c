/* EXPECTED: 42 */
struct Big {
    int f0;  int f1;  int f2;  int f3;  int f4;
    int f5;  int f6;  int f7;  int f8;  int f9;
    int f10; int f11; int f12; int f13; int f14;
    int f15; int f16; int f17; int f18; int f19;
    int f20;
};

int main(void) {
    struct Big b;
    b.f0 = 1;  b.f1 = 1;  b.f2 = 1;  b.f3 = 1;  b.f4 = 1;
    b.f5 = 1;  b.f6 = 1;  b.f7 = 1;  b.f8 = 1;  b.f9 = 1;
    b.f10 = 1; b.f11 = 1; b.f12 = 1; b.f13 = 1; b.f14 = 1;
    b.f15 = 1; b.f16 = 1; b.f17 = 1; b.f18 = 1; b.f19 = 1;
    b.f20 = 22;
    return b.f0 + b.f1 + b.f2 + b.f3 + b.f4 +
           b.f5 + b.f6 + b.f7 + b.f8 + b.f9 +
           b.f10 + b.f11 + b.f12 + b.f13 + b.f14 +
           b.f15 + b.f16 + b.f17 + b.f18 + b.f19 +
           b.f20;
}
