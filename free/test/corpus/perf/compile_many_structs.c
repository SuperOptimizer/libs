/* EXPECTED: 0 */
struct s0 { int a; int b; int c; int d; };
struct s1 { int a; int b; int c; int d; };
struct s2 { int a; int b; int c; int d; };
struct s3 { int a; int b; int c; int d; };
struct s4 { int a; int b; int c; int d; };
struct s5 { int a; int b; int c; int d; };
struct s6 { int a; int b; int c; int d; };
struct s7 { int a; int b; int c; int d; };
struct s8 { int a; int b; int c; int d; };
struct s9 { int a; int b; int c; int d; };
struct s10 { int x; int y; char z; long w; };
struct s11 { int x; int y; char z; long w; };
struct s12 { int x; int y; char z; long w; };
struct s13 { int x; int y; char z; long w; };
struct s14 { int x; int y; char z; long w; };
struct s15 { int x; int y; char z; long w; };
struct s16 { int x; int y; char z; long w; };
struct s17 { int x; int y; char z; long w; };
struct s18 { int x; int y; char z; long w; };
struct s19 { int x; int y; char z; long w; };
struct s20 { int p; int q; int r; int s; int t; };
struct s21 { int p; int q; int r; int s; int t; };
struct s22 { int p; int q; int r; int s; int t; };
struct s23 { int p; int q; int r; int s; int t; };
struct s24 { int p; int q; int r; int s; int t; };
struct s25 { int p; int q; int r; int s; int t; };
struct s26 { int p; int q; int r; int s; int t; };
struct s27 { int p; int q; int r; int s; int t; };
struct s28 { int p; int q; int r; int s; int t; };
struct s29 { int p; int q; int r; int s; int t; };

int main(void) {
    struct s0 a;
    struct s10 b;
    struct s20 c;
    struct s29 d;
    a.a = 1; a.b = 2; a.c = 3; a.d = 4;
    b.x = 10; b.y = 20; b.z = 30; b.w = 40;
    c.p = 100; c.q = 200; c.r = 300; c.s = 400; c.t = 500;
    d.p = 5; d.q = 6; d.r = 7; d.s = 8; d.t = 9;
    return (a.a + a.b + a.c + a.d +
            b.x + b.y + b.z + b.w +
            c.p + c.q + c.r + c.s + c.t +
            d.p + d.q + d.r + d.s + d.t == 1645) ? 0 : 1;
}
