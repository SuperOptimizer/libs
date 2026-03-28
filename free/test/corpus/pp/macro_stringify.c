/* EXPECTED: 5 */
#define STR(x) #x
int main(void) {
    const char *s = STR(hello);
    /* "hello" is 5 characters */
    int len = 0;
    while (s[len] != '\0') {
        len = len + 1;
    }
    return len;
}
