/* GAP-7c: Arena OOM with struct return + multi-param call
 * EXPECTED: compile success, return 42
 * STATUS: FAILS - arena_alloc: out of memory
 *
 * Returning a struct from a function that is called also triggers OOM.
 * Likely related to the same int-params arena bug.
 */

struct point { int x; int y; };

struct point make_point(int x, int y) {
    struct point p;
    p.x = x;
    p.y = y;
    return p;
}

int main(void) {
    struct point p;
    p = make_point(20, 22);
    return p.x + p.y;
}
