/* EXPECTED: 0 */
/* Quicksort 10000 integers (inline implementation, no libc) */
int arr[10000];

void swap(int *a, int *b) {
    int t = *a;
    *a = *b;
    *b = t;
}

int partition(int *a, int lo, int hi) {
    int pivot = a[hi];
    int i = lo - 1;
    int j;
    for (j = lo; j < hi; j++) {
        if (a[j] <= pivot) {
            i++;
            swap(&a[i], &a[j]);
        }
    }
    swap(&a[i + 1], &a[hi]);
    return i + 1;
}

void qsort_impl(int *a, int lo, int hi) {
    if (lo < hi) {
        int p = partition(a, lo, hi);
        qsort_impl(a, lo, p - 1);
        qsort_impl(a, p + 1, hi);
    }
}

int main(void) {
    int n = 10000;
    int i;
    unsigned int seed = 12345;

    /* Simple PRNG to fill array */
    for (i = 0; i < n; i++) {
        seed = seed * 1103515245 + 12345;
        arr[i] = (int)((seed >> 16) & 0x7FFF);
    }

    qsort_impl(arr, 0, n - 1);

    /* Verify sorted */
    for (i = 0; i < n - 1; i++) {
        if (arr[i] > arr[i + 1]) {
            return 1;
        }
    }
    return 0;
}
