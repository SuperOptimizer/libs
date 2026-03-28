/* EXPECTED: 42 */
#define DOUBLE(x) ((x) + (x))
#define INC(x) ((x) + 1)
int main(void) {
    return DOUBLE(INC(20));
}
