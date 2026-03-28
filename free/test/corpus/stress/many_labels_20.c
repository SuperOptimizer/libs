/* EXPECTED: 20 */
int main(void) {
    int x = 0;
    goto L1;
L1: x = x + 1; goto L2;
L2: x = x + 1; goto L3;
L3: x = x + 1; goto L4;
L4: x = x + 1; goto L5;
L5: x = x + 1; goto L6;
L6: x = x + 1; goto L7;
L7: x = x + 1; goto L8;
L8: x = x + 1; goto L9;
L9: x = x + 1; goto L10;
L10: x = x + 1; goto L11;
L11: x = x + 1; goto L12;
L12: x = x + 1; goto L13;
L13: x = x + 1; goto L14;
L14: x = x + 1; goto L15;
L15: x = x + 1; goto L16;
L16: x = x + 1; goto L17;
L17: x = x + 1; goto L18;
L18: x = x + 1; goto L19;
L19: x = x + 1; goto L20;
L20: x = x + 1;
    return x;
}
