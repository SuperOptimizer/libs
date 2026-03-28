/* GAP-9: Preprocessor token pasting (##) not supported
 * EXPECTED: compile success, return 42
 * STATUS: FAILS - error: expected ';', got token kind 2
 *
 * The ## operator for token concatenation in macros is not handled.
 * This is commonly used for generating identifiers programmatically.
 */

#define CONCAT(a, b) a ## b
#define MAKE_FUNC(name) int CONCAT(get_, name)(void) { return 42; }

MAKE_FUNC(value)

int main(void) {
    return CONCAT(get_, value)();
}
