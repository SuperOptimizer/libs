/* GAP-4: Initializer lists not supported
 * EXPECTED: compile success
 * STATUS: FAILS - error: expected expression, got token kind 42
 *
 * Brace-enclosed initializer lists ({...}) are not recognized.
 * This is fundamental C89 syntax used for array and struct initialization.
 */

struct point { int x; int y; };

int main(void) {
    int arr[5] = {1, 2, 3, 4, 5};
    struct point p = {10, 20};
    int zeros[100] = {0};
    char msg[] = {'H', 'i', '\0'};
    return arr[2] + p.y;
}
