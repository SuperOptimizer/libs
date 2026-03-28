/* EXPECTED: 108 */
/* Test string literal concatenation: "foo" "bar" -> "foobar" */

int main(void) {
    char *s;
    s = "hello " "world";
    /* 'l' == 108, s[3] is 'l' in "hello world" */
    return s[3];
}
