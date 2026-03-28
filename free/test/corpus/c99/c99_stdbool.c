/* EXPECTED: 1 */
#include <stdbool.h>
int main(void) {
    bool b = true;
    bool c = false;
    return b && !c;
}
