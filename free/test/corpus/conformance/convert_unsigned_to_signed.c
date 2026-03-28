/* EXPECTED: 42 */
/* unsigned value that fits in signed converts cleanly */
int main(void) {
    unsigned int u = 42;
    int s = (int)u;
    return s;
}
