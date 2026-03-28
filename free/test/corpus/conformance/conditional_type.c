/* EXPECTED: 1 */
/* ternary result type follows usual arithmetic conversions */
int main(void) {
    int s = -1;
    unsigned int u = 1;
    unsigned int result;
    /* condition true: s is converted to unsigned for result type */
    result = 1 ? (unsigned int)s : u;
    /* (unsigned)-1 is UINT_MAX */
    if (result > u)
        return 1;
    return 0;
}
