/* EXPECTED: 1 */
#include <stddef.h>
int main(void) {
    int *p = NULL;
    int x = 5;
    int *q = &x;
    return p == NULL && q != NULL;
}
