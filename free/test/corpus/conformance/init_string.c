/* EXPECTED: 3 */
/* char s[] = "hi"; sizeof includes null terminator */
int main(void) {
    char s[] = "hi";
    return sizeof(s); /* 'h','i','\0' = 3 bytes */
}
