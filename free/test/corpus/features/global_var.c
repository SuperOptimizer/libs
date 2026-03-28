/* EXPECTED: 0 */
int g;
int main(void) {
    return g; /* uninitialized global is zero */
}
