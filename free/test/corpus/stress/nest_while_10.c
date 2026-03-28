/* EXPECTED: 10 */
int main(void) {
    int a=1, b=1, c=1, d=1, e=1;
    int f=1, g=1, h=1, i=1, j=1;
    int count = 0;
    while (a--) {
     while (b--) {
      while (c--) {
       while (d--) {
        while (e--) {
         while (f--) {
          while (g--) {
           while (h--) {
            while (i--) {
             while (j--) {
              count = count + 1;
             }
            }
           }
          }
         }
        }
       }
      }
     }
    }
    return count + 9;
}
