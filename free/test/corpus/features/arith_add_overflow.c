/* EXPECTED: 1 */
#include <limits.h>
int main(void) {
    int x = INT_MAX;
    int y = x + 0;
    return y == INT_MAX;
}
