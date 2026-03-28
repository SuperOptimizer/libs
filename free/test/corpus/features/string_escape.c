/* EXPECTED: 1 */
int main(void) {
    const char *s = "\n\t\\\0";
    int ok = 1;
    if (s[0] != '\n') ok = 0;
    if (s[1] != '\t') ok = 0;
    if (s[2] != '\\') ok = 0;
    if (s[3] != '\0') ok = 0;
    return ok;
}
