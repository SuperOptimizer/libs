/* EXPECTED: 30 */
/* struct initialized with brace-enclosed list */
struct point {
    int x;
    int y;
};

int main(void) {
    struct point p = {10, 20};
    return p.x + p.y; /* 30 */
}
