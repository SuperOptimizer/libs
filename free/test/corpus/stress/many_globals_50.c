/* EXPECTED: 100 */
int g0=2,g1=2,g2=2,g3=2,g4=2,g5=2,g6=2,g7=2,g8=2,g9=2;
int g10=2,g11=2,g12=2,g13=2,g14=2,g15=2,g16=2,g17=2,g18=2,g19=2;
int g20=2,g21=2,g22=2,g23=2,g24=2,g25=2,g26=2,g27=2,g28=2,g29=2;
int g30=2,g31=2,g32=2,g33=2,g34=2,g35=2,g36=2,g37=2,g38=2,g39=2;
int g40=2,g41=2,g42=2,g43=2,g44=2,g45=2,g46=2,g47=2,g48=2,g49=2;

int main(void) {
    int sum = 0;
    sum = g0+g1+g2+g3+g4+g5+g6+g7+g8+g9;
    sum = sum+g10+g11+g12+g13+g14+g15+g16+g17+g18+g19;
    sum = sum+g20+g21+g22+g23+g24+g25+g26+g27+g28+g29;
    sum = sum+g30+g31+g32+g33+g34+g35+g36+g37+g38+g39;
    sum = sum+g40+g41+g42+g43+g44+g45+g46+g47+g48+g49;
    return sum;
}
