/* EXPECTED: 0 */
/* Test: large switch with 80 cases, values > 4095, nested, gaps */

int classify(int x) {
    switch (x) {
    case 0: return 10;
    case 1: return 11;
    case 2: return 12;
    case 3: return 13;
    case 4: return 14;
    case 5: return 15;
    case 6: return 16;
    case 7: return 17;
    case 8: return 18;
    case 9: return 19;
    case 10: return 20;
    case 11: return 21;
    case 12: return 22;
    case 13: return 23;
    case 14: return 24;
    case 15: return 25;
    case 16: return 26;
    case 17: return 27;
    case 18: return 28;
    case 19: return 29;
    case 20: return 30;
    case 21: return 31;
    case 22: return 32;
    case 23: return 33;
    case 24: return 34;
    case 25: return 35;
    case 26: return 36;
    case 27: return 37;
    case 28: return 38;
    case 29: return 39;
    case 30: return 40;
    case 31: return 41;
    case 32: return 42;
    case 33: return 43;
    case 34: return 44;
    case 35: return 45;
    case 36: return 46;
    case 37: return 47;
    case 38: return 48;
    case 39: return 49;
    case 40: return 50;
    case 41: return 51;
    case 42: return 52;
    case 43: return 53;
    case 44: return 54;
    case 45: return 55;
    case 46: return 56;
    case 47: return 57;
    case 48: return 58;
    case 49: return 59;
    case 50: return 60;
    case 51: return 61;
    case 52: return 62;
    case 53: return 63;
    case 54: return 64;
    case 55: return 65;
    case 56: return 66;
    case 57: return 67;
    case 58: return 68;
    case 59: return 69;
    case 60: return 70;
    case 61: return 71;
    case 62: return 72;
    case 63: return 73;
    case 64: return 74;
    case 65: return 75;
    case 66: return 76;
    case 67: return 77;
    case 68: return 78;
    case 69: return 79;
    case 70: return 80;
    case 71: return 81;
    case 72: return 82;
    case 73: return 83;
    case 74: return 84;
    case 75: return 85;
    case 76: return 86;
    case 77: return 87;
    case 78: return 88;
    case 79: return 89;
    default: return -1;
    }
}

int large_vals(int x) {
    switch (x) {
    case 0: return 0;
    case 4095: return 1;
    case 4096: return 2;
    case 10000: return 3;
    case 65535: return 4;
    case 100000: return 5;
    default: return -1;
    }
}

int gaps(int x) {
    switch (x) {
    case 1: return 10;
    case 100: return 20;
    case 1000: return 30;
    default: return -1;
    }
}

int nested(int x, int y) {
    switch (x) {
    case 0:
        switch (y) {
        case 0: return 0;
        case 1: return 1;
        default: return 2;
        }
    case 1: return 10;
    default: return 20;
    }
}

int main(void) {
    int ok;
    ok = 1;
    /* large switch */
    if (classify(0) != 10) ok = 0;
    if (classify(42) != 52) ok = 0;
    if (classify(79) != 89) ok = 0;
    if (classify(80) != -1) ok = 0;
    /* large case values */
    if (large_vals(0) != 0) ok = 0;
    if (large_vals(4095) != 1) ok = 0;
    if (large_vals(4096) != 2) ok = 0;
    if (large_vals(10000) != 3) ok = 0;
    if (large_vals(65535) != 4) ok = 0;
    if (large_vals(100000) != 5) ok = 0;
    if (large_vals(999) != -1) ok = 0;
    /* gaps */
    if (gaps(1) != 10) ok = 0;
    if (gaps(100) != 20) ok = 0;
    if (gaps(1000) != 30) ok = 0;
    if (gaps(50) != -1) ok = 0;
    /* nested */
    if (nested(0, 0) != 0) ok = 0;
    if (nested(0, 1) != 1) ok = 0;
    if (nested(0, 5) != 2) ok = 0;
    if (nested(1, 0) != 10) ok = 0;
    if (nested(5, 0) != 20) ok = 0;
    return ok ? 0 : 1;
}
