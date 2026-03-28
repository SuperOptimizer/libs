/* GAP-3a: Function pointer as parameter type
 * EXPECTED: compile success
 * STATUS: FAILS - error: expected ')', got token kind 40
 *
 * Function pointers work as local variables and in calls, but cannot be
 * used as function parameter types. This blocks qsort, signal, and all
 * callback-based APIs.
 */

int less(int a, int b) { return a < b; }

void sort(int *arr, int n, int (*cmp)(int, int)) {
    /* body doesn't matter -- parse fails at declaration */
    return;
}

int main(void) {
    int arr[3];
    arr[0] = 3;
    arr[1] = 1;
    arr[2] = 2;
    sort(arr, 3, less);
    return arr[0];
}
