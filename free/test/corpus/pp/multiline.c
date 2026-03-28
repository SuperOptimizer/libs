/* EXPECTED: 42 */
#define MULTI(a, b) \
    ((a) + \
     (b))
int main(void) {
    return MULTI(20, 22);
}
