/* EXPECTED: 0 */
/* Test implicit function declaration (C89 behavior) */
int main()
{
    /* foo is not declared yet - should implicitly declare as int foo() */
    int x;
    x = foo();
    if (x != 42) return 1;
    return 0;
}

int foo()
{
    return 42;
}
