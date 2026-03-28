/* EXPECTED: 1 */
#include <limits.h>
int main(void) {
    int x = INT_MIN;
    int y = x + 1;
    /* y = INT_MIN+1, which is negative */
    /* INT_MIN < y is true */
    return (x < y) ? 1 : 0;
}
