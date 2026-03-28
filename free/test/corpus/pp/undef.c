/* EXPECTED: 10 */
#define X 99
#undef X
#define X 10
int main(void) {
    return X;
}
