/* EXPECTED: 65 */
int main(void) {
    char c = 'A';
    unsigned char uc = 200;
    int x = c;       /* 65 */
    int y = (int)uc;  /* 200 */
    /* verify char holds correct ASCII value */
    return x;
}
