/* EXPECTED: 30 */
/* K&R old-style function definition */
int add(a, b)
    int a;
    int b;
{
    return a + b;
}

int main(void) {
    return add(10, 20); /* 30 */
}
