/* GAP-7b: Arena OOM with 3 int parameters
 * EXPECTED: compile success, return 42
 * STATUS: FAILS - arena_alloc: out of memory
 *
 * Same root cause as gap7_arena_oom_int_params.c but with 3 parameters.
 */

int sum3(int a, int b, int c) {
    return a + b + c;
}

int main(void) {
    return sum3(10, 20, 12);
}
