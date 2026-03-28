/* EXPECTED: 210 */
struct big {
    int m0; int m1; int m2; int m3; int m4;
    int m5; int m6; int m7; int m8; int m9;
    int m10; int m11; int m12; int m13; int m14;
    int m15; int m16; int m17; int m18; int m19;
};

int main(void) {
    struct big b;
    b.m0=1; b.m1=2; b.m2=3; b.m3=4; b.m4=5;
    b.m5=6; b.m6=7; b.m7=8; b.m8=9; b.m9=10;
    b.m10=11; b.m11=12; b.m12=13; b.m13=14; b.m14=15;
    b.m15=16; b.m16=17; b.m17=18; b.m18=19; b.m19=20;
    return b.m0+b.m1+b.m2+b.m3+b.m4+b.m5+b.m6+b.m7+b.m8+b.m9
          +b.m10+b.m11+b.m12+b.m13+b.m14+b.m15+b.m16+b.m17+b.m18+b.m19;
}
