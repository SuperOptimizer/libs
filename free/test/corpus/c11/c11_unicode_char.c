/* EXPECTED: 65 */
typedef unsigned short char16_t;
typedef unsigned int char32_t;
int main(void) {
    char32_t ch = U'A';
    return (int)ch;
}
