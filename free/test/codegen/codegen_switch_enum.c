/* EXPECTED: 42 */
enum Color { RED, GREEN, BLUE, YELLOW };

int val(enum Color c) {
    switch (c) {
        case RED:    return 10;
        case GREEN:  return 42;
        case BLUE:   return 20;
        case YELLOW: return 30;
        default:     return 0;
    }
}

int main(void) {
    return val(GREEN);
}
