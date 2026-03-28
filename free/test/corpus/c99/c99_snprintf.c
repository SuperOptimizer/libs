/* EXPECTED: 1 */
int snprintf(char *buf, unsigned long size, const char *fmt, ...);
int strcmp(const char *a, const char *b);
int main(void) {
    char buf[4];
    snprintf(buf, sizeof(buf), "hello");
    return (buf[0] == 'h' && buf[1] == 'e' && buf[2] == 'l' && buf[3] == '\0');
}
