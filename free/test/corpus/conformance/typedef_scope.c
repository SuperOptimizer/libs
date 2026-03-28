/* EXPECTED: 4 */
/* typedef in inner scope shadows outer typedef */
typedef int MyType;

int main(void) {
    MyType a = 1;
    {
        typedef long MyType;
        MyType b = 3;
        a = a + (int)b; /* 1 + 3 = 4 */
    }
    return a;
}
