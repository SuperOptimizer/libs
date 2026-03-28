/* EXPECTED: 0 */
/* Test static struct with bitfield initializers */
struct bf {
    int x : 4;
    int y : 4;
};

static struct bf s = { 5, 3 };

int main()
{
    if (s.x != 5) return 1;
    if (s.y != 3) return 2;
    return 0;
}
