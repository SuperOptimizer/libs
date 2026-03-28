/* EXPECTED: 30 */
struct point {
    int x;
    int y;
};
int sum_point(struct point p) {
    return p.x + p.y;
}
int main(void) {
    struct point p;
    p.x = 10;
    p.y = 20;
    return sum_point(p);
}
