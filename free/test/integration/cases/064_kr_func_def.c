/* EXPECTED: 0 */
/* Test K&R old-style function definitions */
int add(a, b)
    int a;
    int b;
{
    return a + b;
}

int main()
{
    if (add(3, 4) != 7) return 1;
    return 0;
}
