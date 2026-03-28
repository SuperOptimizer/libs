/* EXPECTED: 42 */
#include "inc_outer.h"
int main(void) {
    /* inc_outer.h includes inc_inner.h, so both macros available */
    return OUTER_VAL + INNER_VAL;
}
