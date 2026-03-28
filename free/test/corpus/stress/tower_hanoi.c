/* EXPECTED: 255 */
int hanoi_moves(int n) {
    /* hanoi(n) needs 2^n - 1 moves */
    int result = 1;
    int i;
    for (i = 0; i < n; i++) {
        result = result * 2;
    }
    return result - 1;
}

int main(void) {
    int moves = hanoi_moves(10);
    /* 2^10 - 1 = 1023, 1023 & 255 = 255 */
    return moves & 255;
}
