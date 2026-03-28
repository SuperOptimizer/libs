/* EXPECTED: 50 */
int main(void) {
    char *s0="a"; char *s1="b"; char *s2="c"; char *s3="d"; char *s4="e";
    char *s5="f"; char *s6="g"; char *s7="h"; char *s8="i"; char *s9="j";
    char *s10="k"; char *s11="l"; char *s12="m"; char *s13="n"; char *s14="o";
    char *s15="p"; char *s16="q"; char *s17="r"; char *s18="s"; char *s19="t";
    char *s20="u"; char *s21="v"; char *s22="w"; char *s23="x"; char *s24="y";
    char *s25="z"; char *s26="A"; char *s27="B"; char *s28="C"; char *s29="D";
    char *s30="E"; char *s31="F"; char *s32="G"; char *s33="H"; char *s34="I";
    char *s35="J"; char *s36="K"; char *s37="L"; char *s38="M"; char *s39="N";
    char *s40="O"; char *s41="P"; char *s42="Q"; char *s43="R"; char *s44="S";
    char *s45="T"; char *s46="U"; char *s47="V"; char *s48="W"; char *s49="X";
    int count = 0;
    count = count + (s0[0]!=0) + (s1[0]!=0) + (s2[0]!=0) + (s3[0]!=0) + (s4[0]!=0);
    count = count + (s5[0]!=0) + (s6[0]!=0) + (s7[0]!=0) + (s8[0]!=0) + (s9[0]!=0);
    count = count + (s10[0]!=0)+(s11[0]!=0)+(s12[0]!=0)+(s13[0]!=0)+(s14[0]!=0);
    count = count + (s15[0]!=0)+(s16[0]!=0)+(s17[0]!=0)+(s18[0]!=0)+(s19[0]!=0);
    count = count + (s20[0]!=0)+(s21[0]!=0)+(s22[0]!=0)+(s23[0]!=0)+(s24[0]!=0);
    count = count + (s25[0]!=0)+(s26[0]!=0)+(s27[0]!=0)+(s28[0]!=0)+(s29[0]!=0);
    count = count + (s30[0]!=0)+(s31[0]!=0)+(s32[0]!=0)+(s33[0]!=0)+(s34[0]!=0);
    count = count + (s35[0]!=0)+(s36[0]!=0)+(s37[0]!=0)+(s38[0]!=0)+(s39[0]!=0);
    count = count + (s40[0]!=0)+(s41[0]!=0)+(s42[0]!=0)+(s43[0]!=0)+(s44[0]!=0);
    count = count + (s45[0]!=0)+(s46[0]!=0)+(s47[0]!=0)+(s48[0]!=0)+(s49[0]!=0);
    return count;
}
