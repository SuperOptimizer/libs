/* EXPECTED: 43 */
/* Sum 1 to 10,000,000 in a loop, return checksum */
int main(void) {
    long sum = 0;
    int i;
    for (i = 1; i <= 10000000; i++) {
        sum += i;
    }
    /* sum = 10000000 * 10000001 / 2 = 50000005000000 */
    /* Extract a stable 8-bit checksum */
    return (int)((sum >> 8) & 0xFF) ^ (int)(sum & 0xFF);
}
