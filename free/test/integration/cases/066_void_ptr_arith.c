/* EXPECTED: 0 */
/* Test void pointer arithmetic (GCC extension, add 1 byte) */
int main()
{
    int arr[2];
    void *p;
    arr[0] = 0x01020304;
    arr[1] = 0x05060708;
    p = (void *)arr;
    /* void* + 4 should advance 4 bytes (sizeof(int)) */
    p = p + 4;
    if (*(int *)p != 0x05060708) return 1;
    return 0;
}
