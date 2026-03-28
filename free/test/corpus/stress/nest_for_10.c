/* EXPECTED: 10 */
int main(void) {
    int a, b, c, d, e, f, g, h, i, j;
    int count = 0;
    for (a=0; a<1; a++)
     for (b=0; b<1; b++)
      for (c=0; c<1; c++)
       for (d=0; d<1; d++)
        for (e=0; e<1; e++)
         for (f=0; f<2; f++)
          for (g=0; g<1; g++)
           for (h=0; h<1; h++)
            for (i=0; i<1; i++)
             for (j=0; j<5; j++)
              count = count + 1;
    return count;
}
