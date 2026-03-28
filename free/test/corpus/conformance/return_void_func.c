/* EXPECTED: 1 */
/* return; (without value) is valid in void function */
int flag;

void set_flag(int val) {
    if (val < 0)
        return; /* early return from void function */
    flag = val;
}

int main(void) {
    flag = 0;
    set_flag(1);
    set_flag(-1); /* early return, flag unchanged */
    return flag; /* 1 */
}
