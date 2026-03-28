/* EXPECTED: 10 */
int main(void) {
    int i = 0;
loop:
    if (i >= 10)
        goto end;
    i = i + 1;
    goto loop;
end:
    return i;
}
