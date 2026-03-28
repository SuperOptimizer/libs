/* EXPECTED: 42 */
int main(void) {
    goto done;
    return 0;
done:
    return 42;
}
