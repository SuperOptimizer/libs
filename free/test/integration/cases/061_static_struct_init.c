/* EXPECTED: 42 */
/* Test: static struct initializer with mixed types */
struct config {
    char *name;
    int value;
    int flags;
};

static struct config cfg = {"hello", 35, 7};

int main(void) {
    if (cfg.name[0] != 'h') return 1;
    if (cfg.name[4] != 'o') return 2;
    if (cfg.value != 35) return 3;
    if (cfg.flags != 7) return 4;
    return cfg.value + cfg.flags;
}
