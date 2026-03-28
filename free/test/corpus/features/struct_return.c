/* EXPECTED: 30 */
struct point {
    int x;
    int y;
};
struct point make_point(int x, int y) {
    struct point p;
    p.x = x;
    p.y = y;
    return p;
}
int main(void) {
    struct point p = make_point(10, 20);
    return p.x + p.y;
}
