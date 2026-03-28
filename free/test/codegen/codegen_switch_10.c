/* EXPECTED: 42 */
int pick(int n) {
    switch (n) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 4;
        case 3: return 9;
        case 4: return 16;
        case 5: return 25;
        case 6: return 36;
        case 7: return 42;
        case 8: return 64;
        case 9: return 81;
        default: return 100;
    }
}

int main(void) {
    return pick(7);
}
