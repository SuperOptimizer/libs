/* EXPECTED: 42 */
#define PASTE(a, b) a##b
int main(void) {
    int xy = 42;
    return PASTE(x, y);
}
