/* EXPECTED: 30 */
/* Statement expressions in macros -- the kernel's primary use case */
#define MAX(a, b) ({ \
    int _a = (a);    \
    int _b = (b);    \
    _a > _b ? _a : _b; \
})

#define MIN(a, b) ({ \
    int _a = (a);    \
    int _b = (b);    \
    _a < _b ? _a : _b; \
})

int main(void) {
    int x = 10;
    int y = 20;
    int z = 30;
    int m;
    m = MAX(MAX(x, y), z);
    return m;
}
