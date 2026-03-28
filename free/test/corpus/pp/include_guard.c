/* EXPECTED: 42 */
#include "inc_guarded.h"
#include "inc_guarded.h"
int main(void) {
    /* GUARDED_VAL defined once as 21 despite double include */
    /* GUARD_DOUBLE not defined because first include sees GUARDED_VAL undefined */
    int x = GUARDED_VAL;
    return x + 21;
}
