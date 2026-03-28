/* GAP-7: Arena OOM when calling function with 2+ int parameters
 * EXPECTED: compile success, return 42
 * STATUS: FAILS - arena_alloc: out of memory
 *
 * Functions with multiple consecutive 'int' parameters trigger arena
 * exhaustion during codegen/IR. Other type combinations work fine:
 *   int f(int a)            -- OK
 *   int f(int *a, int b)    -- OK
 *   int f(char a, int b)    -- OK
 *   int f(long a, int b)    -- OK
 *   int f(int a, int b)     -- OOM
 *   int f(int a, int b, int c) -- OOM
 *
 * This is likely an infinite loop or exponential blowup in the IR/codegen
 * when resolving type-identical parameters.
 */

int add(int a, int b) {
    return a + b;
}

int main(void) {
    return add(20, 22);
}
