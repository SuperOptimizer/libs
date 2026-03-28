/* EXPECTED: 21 */
/* Test: array of structs with subscript member access */
struct S { int x; int y; };

int main(void) {
    struct S arr[3];
    arr[0].x = 10; arr[0].y = 1;
    arr[1].x = 20; arr[1].y = 2;
    arr[2].x = 5;  arr[2].y = 4;
    return arr[0].x + arr[1].y + arr[2].x + arr[2].y;
}
