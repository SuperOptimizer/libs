/* EXPECTED: 42 */
int main(void) {
    void *table[3];
    int val;
    val = 0;

    table[0] = &&step1;
    table[1] = &&step2;
    table[2] = &&done;

    goto *table[0];

step1:
    val = val + 10;
    goto *table[1];

step2:
    val = val + 32;
    goto *table[2];

done:
    return val;
}
