/* EXPECTED: 50 */
int my_strlen(char *s) {
    int n;
    n = 0;
    while (s[n]) {
        n = n + 1;
    }
    return n;
}

int main(void) {
    char *s0  = "a";
    char *s1  = "bb";
    char *s2  = "ccc";
    char *s3  = "dddd";
    char *s4  = "e";
    char *s5  = "ff";
    char *s6  = "ggg";
    char *s7  = "hhhh";
    char *s8  = "i";
    char *s9  = "jj";
    char *s10 = "kkk";
    char *s11 = "llll";
    char *s12 = "m";
    char *s13 = "nn";
    char *s14 = "ooo";
    char *s15 = "pppp";
    char *s16 = "q";
    char *s17 = "rr";
    char *s18 = "sss";
    char *s19 = "tttt";
    return my_strlen(s0) + my_strlen(s1) + my_strlen(s2) +
           my_strlen(s3) + my_strlen(s4) + my_strlen(s5) +
           my_strlen(s6) + my_strlen(s7) + my_strlen(s8) +
           my_strlen(s9) + my_strlen(s10) + my_strlen(s11) +
           my_strlen(s12) + my_strlen(s13) + my_strlen(s14) +
           my_strlen(s15) + my_strlen(s16) + my_strlen(s17) +
           my_strlen(s18) + my_strlen(s19);
}
