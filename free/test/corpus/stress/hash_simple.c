/* EXPECTED: 210 */
unsigned int simple_hash(char *s) {
    unsigned int h = 0;
    while (*s) {
        h = h * 31 + (unsigned int)*s;
        s++;
    }
    return h;
}

int main(void) {
    char str[6];
    unsigned int h;
    str[0] = 'h';
    str[1] = 'e';
    str[2] = 'l';
    str[3] = 'l';
    str[4] = 'o';
    str[5] = '\0';
    h = simple_hash(str);
    /* hash("hello") = 99162322, 99162322 & 255 = 210 */
    return (int)(h & 255);
}
