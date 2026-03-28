/* EXPECTED: 152 */
/* strlen on a 10000 char string, 1000 times */
char buf[10001];

int my_strlen(char *s) {
    int len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

int main(void) {
    int i;
    int total = 0;

    /* Fill buffer with non-zero chars */
    for (i = 0; i < 10000; i++) {
        buf[i] = (char)((i % 255) + 1);
    }
    buf[10000] = '\0';

    /* Call strlen 1000 times */
    for (i = 0; i < 1000; i++) {
        total += my_strlen(buf);
    }
    /* total = 10000 * 1000 = 10000000 */
    return (total >> 16) & 0xFF;
}
