/* EXPECTED: 50 */
int f0(int x) { return x + 1; }
int f1(int x) { return f0(x) + 1; }
int f2(int x) { return f1(x) + 1; }
int f3(int x) { return f2(x) + 1; }
int f4(int x) { return f3(x) + 1; }
int f5(int x) { return f4(x) + 1; }
int f6(int x) { return f5(x) + 1; }
int f7(int x) { return f6(x) + 1; }
int f8(int x) { return f7(x) + 1; }
int f9(int x) { return f8(x) + 1; }
int f10(int x) { return f9(x) + 1; }
int f11(int x) { return f10(x) + 1; }
int f12(int x) { return f11(x) + 1; }
int f13(int x) { return f12(x) + 1; }
int f14(int x) { return f13(x) + 1; }
int f15(int x) { return f14(x) + 1; }
int f16(int x) { return f15(x) + 1; }
int f17(int x) { return f16(x) + 1; }
int f18(int x) { return f17(x) + 1; }
int f19(int x) { return f18(x) + 1; }
int f20(int x) { return f19(x) + 1; }
int f21(int x) { return f20(x) + 1; }
int f22(int x) { return f21(x) + 1; }
int f23(int x) { return f22(x) + 1; }
int f24(int x) { return f23(x) + 1; }
int f25(int x) { return f24(x) + 1; }
int f26(int x) { return f25(x) + 1; }
int f27(int x) { return f26(x) + 1; }
int f28(int x) { return f27(x) + 1; }
int f29(int x) { return f28(x) + 1; }
int f30(int x) { return f29(x) + 1; }
int f31(int x) { return f30(x) + 1; }
int f32(int x) { return f31(x) + 1; }
int f33(int x) { return f32(x) + 1; }
int f34(int x) { return f33(x) + 1; }
int f35(int x) { return f34(x) + 1; }
int f36(int x) { return f35(x) + 1; }
int f37(int x) { return f36(x) + 1; }
int f38(int x) { return f37(x) + 1; }
int f39(int x) { return f38(x) + 1; }
int f40(int x) { return f39(x) + 1; }
int f41(int x) { return f40(x) + 1; }
int f42(int x) { return f41(x) + 1; }
int f43(int x) { return f42(x) + 1; }
int f44(int x) { return f43(x) + 1; }
int f45(int x) { return f44(x) + 1; }
int f46(int x) { return f45(x) + 1; }
int f47(int x) { return f46(x) + 1; }
int f48(int x) { return f47(x) + 1; }

int main(void) {
    return f48(1);
}
