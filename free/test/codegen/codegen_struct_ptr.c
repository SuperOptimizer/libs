/* EXPECTED: 42 */
struct Point {
    int x;
    int y;
};

int main(void) {
    struct Point s;
    struct Point *p;
    p = &s;
    p->x = 19;
    p->y = 23;
    return p->x + p->y;
}
