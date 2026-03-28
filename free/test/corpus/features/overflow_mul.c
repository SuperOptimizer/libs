/* EXPECTED: 1 */
int main(void) {
    long a, b, result;
    int ov;
    a = 9223372036854775807L; /* LONG_MAX */
    b = 2;
    ov = __builtin_mul_overflow(a, b, &result);
    if (!ov) return 0;
    /* non-overflow case */
    a = 7;
    b = 6;
    ov = __builtin_mul_overflow(a, b, &result);
    if (ov) return 0;
    if (result != 42) return 0;
    return 1;
}
