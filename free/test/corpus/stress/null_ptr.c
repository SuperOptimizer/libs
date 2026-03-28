/* EXPECTED: 1 */
#include <stddef.h>
int main(void) {
    int x = 5;
    int *p = &x;
    int *q = NULL;
    int result = 0;
    if (p != NULL) {
        result = result + 1;
    }
    if (q == NULL) {
        result = result + 0;
    }
    if (q != NULL) {
        result = result + 99;
    }
    return result;
}
