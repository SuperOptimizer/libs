/* EXPECTED: 4 */
int my_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}
int main(void) {
    return my_strlen(__func__);
}
