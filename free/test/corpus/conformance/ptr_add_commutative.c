/* EXPECTED: 1 */
/* a[i] == *(a+i) and *(i+a) == i[a]: pointer add is commutative */
int main(void) {
    int arr[5];
    int i = 3;
    int v1, v2, v3, v4;
    arr[3] = 42;
    v1 = arr[i];
    v2 = *(arr + i);
    v3 = *(i + arr);
    v4 = i[arr];
    if (v1 == 42 && v2 == 42 && v3 == 42 && v4 == 42)
        return 1;
    return 0;
}
