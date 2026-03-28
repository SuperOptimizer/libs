/* EXPECTED: 42 */
struct Point {
    int x;
    int y;
};

int main(void) {
    struct Point s;
    s.x = 20;
    s.y = 22;
    return s.x + s.y;
}
