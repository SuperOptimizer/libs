/* EXPECTED: 1 */
#include <limits.h>
int main(void) {
    int x = INT_MAX;
    int y = x - 1;
    int z = x - INT_MAX;
    /* y = INT_MAX-1, z = 0 */
    /* check that x > y and z == 0 */
    return (x > y) && (z == 0);
}
