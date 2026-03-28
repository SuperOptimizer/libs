/* GAP-1: Backslash-newline continuation in preprocessor macros
 * EXPECTED: compile success
 * STATUS: FAILS - lexer error: unexpected character '\' (92)
 *
 * This is the most critical gap. Nearly all real-world C headers use
 * multiline macros with backslash continuation. assert.h, most library
 * headers, and virtually all complex macro definitions depend on this.
 */

#define SWAP(a, b) \
    do { \
        int tmp = (a); \
        (a) = (b); \
        (b) = tmp; \
    } while (0)

int main(void) {
    int x = 1;
    int y = 2;
    SWAP(x, y);
    return x;
}
