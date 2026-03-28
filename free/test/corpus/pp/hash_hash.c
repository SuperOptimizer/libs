/* EXPECTED: 42 */
#define GLUE(a, b) a##b
#define GLUE3(a, b, c) a##b##c
int main(void) {
    int abc = 42;
    int val;
    val = GLUE3(a, b, c);
    return val;
}
