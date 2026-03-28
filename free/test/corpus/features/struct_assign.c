/* EXPECTED: 30 */
struct point {
    int x;
    int y;
};
int main(void) {
    struct point a;
    struct point b;
    a.x = 10;
    a.y = 20;
    b = a; /* struct copy */
    return b.x + b.y;
}
