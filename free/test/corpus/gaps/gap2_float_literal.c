/* GAP-2: Floating-point literals not lexed
 * EXPECTED: compile success
 * STATUS: FAILS - error: expected member name, got token kind 0
 *
 * The lexer does not recognize floating-point literal syntax (3.14, 3.14f,
 * 1e10, 1.5e-3, etc.). The float/double types themselves work when
 * assigned integer values. Only the literal syntax is broken.
 */

int main(void) {
    double d = 3.14;
    float f = 2.5;
    double e = 1e10;
    return (int)(d + f);
}
