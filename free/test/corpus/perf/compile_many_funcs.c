/* EXPECTED: 0 */
int f0(void) { return 0; }
int f1(void) { return 1; }
int f2(void) { return 2; }
int f3(void) { return 3; }
int f4(void) { return 4; }
int f5(void) { return 5; }
int f6(void) { return 6; }
int f7(void) { return 7; }
int f8(void) { return 8; }
int f9(void) { return 9; }
int f10(void) { return 10; }
int f11(void) { return 11; }
int f12(void) { return 12; }
int f13(void) { return 13; }
int f14(void) { return 14; }
int f15(void) { return 15; }
int f16(void) { return 16; }
int f17(void) { return 17; }
int f18(void) { return 18; }
int f19(void) { return 19; }
int f20(void) { return 20; }
int f21(void) { return 21; }
int f22(void) { return 22; }
int f23(void) { return 23; }
int f24(void) { return 24; }
int f25(void) { return 25; }
int f26(void) { return 26; }
int f27(void) { return 27; }
int f28(void) { return 28; }
int f29(void) { return 29; }
int f30(void) { return 30; }
int f31(void) { return 31; }
int f32(void) { return 32; }
int f33(void) { return 33; }
int f34(void) { return 34; }
int f35(void) { return 35; }
int f36(void) { return 36; }
int f37(void) { return 37; }
int f38(void) { return 38; }
int f39(void) { return 39; }
int f40(void) { return 40; }
int f41(void) { return 41; }
int f42(void) { return 42; }
int f43(void) { return 43; }
int f44(void) { return 44; }
int f45(void) { return 45; }
int f46(void) { return 46; }
int f47(void) { return 47; }
int f48(void) { return 48; }
int f49(void) { return 49; }
int f50(void) { return 50; }
int f51(void) { return 51; }
int f52(void) { return 52; }
int f53(void) { return 53; }
int f54(void) { return 54; }
int f55(void) { return 55; }
int f56(void) { return 56; }
int f57(void) { return 57; }
int f58(void) { return 58; }
int f59(void) { return 59; }
int f60(void) { return 60; }
int f61(void) { return 61; }
int f62(void) { return 62; }
int f63(void) { return 63; }
int f64(void) { return 64; }
int f65(void) { return 65; }
int f66(void) { return 66; }
int f67(void) { return 67; }
int f68(void) { return 68; }
int f69(void) { return 69; }
int f70(void) { return 70; }
int f71(void) { return 71; }
int f72(void) { return 72; }
int f73(void) { return 73; }
int f74(void) { return 74; }
int f75(void) { return 75; }
int f76(void) { return 76; }
int f77(void) { return 77; }
int f78(void) { return 78; }
int f79(void) { return 79; }
int f80(void) { return 80; }
int f81(void) { return 81; }
int f82(void) { return 82; }
int f83(void) { return 83; }
int f84(void) { return 84; }
int f85(void) { return 85; }
int f86(void) { return 86; }
int f87(void) { return 87; }
int f88(void) { return 88; }
int f89(void) { return 89; }
int f90(void) { return 90; }
int f91(void) { return 91; }
int f92(void) { return 92; }
int f93(void) { return 93; }
int f94(void) { return 94; }
int f95(void) { return 95; }
int f96(void) { return 96; }
int f97(void) { return 97; }
int f98(void) { return 98; }
int f99(void) { return 99; }
int main(void) {
    int sum = 0;
    sum += f0() + f1() + f2() + f3() + f4();
    sum += f5() + f6() + f7() + f8() + f9();
    sum += f10() + f11() + f12() + f13() + f14();
    sum += f15() + f16() + f17() + f18() + f19();
    sum += f20() + f21() + f22() + f23() + f24();
    sum += f25() + f26() + f27() + f28() + f29();
    sum += f30() + f31() + f32() + f33() + f34();
    sum += f35() + f36() + f37() + f38() + f39();
    sum += f40() + f41() + f42() + f43() + f44();
    sum += f45() + f46() + f47() + f48() + f49();
    sum += f50() + f51() + f52() + f53() + f54();
    sum += f55() + f56() + f57() + f58() + f59();
    sum += f60() + f61() + f62() + f63() + f64();
    sum += f65() + f66() + f67() + f68() + f69();
    sum += f70() + f71() + f72() + f73() + f74();
    sum += f75() + f76() + f77() + f78() + f79();
    sum += f80() + f81() + f82() + f83() + f84();
    sum += f85() + f86() + f87() + f88() + f89();
    sum += f90() + f91() + f92() + f93() + f94();
    sum += f95() + f96() + f97() + f98() + f99();
    /* sum = 4950, 4950 % 256 = 106, but we want exit 0 for simplicity */
    return (sum == 4950) ? 0 : 1;
}
