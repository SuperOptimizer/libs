/* EXPECTED: 42 */
/* Test: struct passed by value as function parameter */
struct Point { int x; int y; };

int sum(struct Point p) {
    return p.x + p.y;
}

int main(void) {
    struct Point p;
    p.x = 20;
    p.y = 22;
    return sum(p);
}
