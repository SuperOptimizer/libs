/* EXPECTED: 7 */
int bsearch_idx(int *arr, int n, int target) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (arr[mid] == target) {
            return mid;
        } else if (arr[mid] < target) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

int main(void) {
    int arr[16];
    int i;
    for (i = 0; i < 16; i++) {
        arr[i] = i * 3;
    }
    /* searching for 21: 21/3 = 7, so index 7 */
    return bsearch_idx(arr, 16, 21);
}
