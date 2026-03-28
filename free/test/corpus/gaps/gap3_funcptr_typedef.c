/* GAP-3b: Function pointer typedef
 * EXPECTED: compile success
 * STATUS: FAILS - error: expected ';', got token kind 40
 *
 * typedef of function pointer types fails. This blocks signal.h and
 * many common C patterns for callback registration.
 */

typedef int (*compare_fn)(int, int);
typedef void (*handler_t)(int);

int less(int a, int b) { return a < b; }

int main(void) {
    compare_fn cmp;
    cmp = less;
    return 0;
}
