/* EXPECTED: 1 */
int main(void) {
    long a, b, result;
    int ov;
    a = 9223372036854775807L; /* LONG_MAX */
    b = 1;
    ov = __builtin_add_overflow(a, b, &result);
    if (!ov) return 0;
    /* also check non-overflow case */
    a = 10;
    b = 20;
    ov = __builtin_add_overflow(a, b, &result);
    if (ov) return 0;
    if (result != 30) return 0;
    return 1;
}
