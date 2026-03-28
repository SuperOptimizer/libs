/* EXPECTED: 20 */
/* Nested preprocessor conditionals 20 deep to stress the preprocessor */
#define LEVEL 20

#if LEVEL >= 1
#define V1 1
 #if LEVEL >= 2
 #define V2 1
  #if LEVEL >= 3
  #define V3 1
   #if LEVEL >= 4
   #define V4 1
    #if LEVEL >= 5
    #define V5 1
     #if LEVEL >= 6
     #define V6 1
      #if LEVEL >= 7
      #define V7 1
       #if LEVEL >= 8
       #define V8 1
        #if LEVEL >= 9
        #define V9 1
         #if LEVEL >= 10
         #define V10 1
          #if LEVEL >= 11
          #define V11 1
           #if LEVEL >= 12
           #define V12 1
            #if LEVEL >= 13
            #define V13 1
             #if LEVEL >= 14
             #define V14 1
              #if LEVEL >= 15
              #define V15 1
               #if LEVEL >= 16
               #define V16 1
                #if LEVEL >= 17
                #define V17 1
                 #if LEVEL >= 18
                 #define V18 1
                  #if LEVEL >= 19
                  #define V19 1
                   #if LEVEL >= 20
                   #define V20 1
                   #else
                   #define V20 0
                   #endif
                  #else
                  #define V19 0
                  #define V20 0
                  #endif
                 #else
                 #define V18 0
                 #define V19 0
                 #define V20 0
                 #endif
                #else
                #define V17 0
                #define V18 0
                #define V19 0
                #define V20 0
                #endif
               #else
               #define V16 0
               #define V17 0
               #define V18 0
               #define V19 0
               #define V20 0
               #endif
              #else
              #define V15 0
              #define V16 0
              #define V17 0
              #define V18 0
              #define V19 0
              #define V20 0
              #endif
             #else
             #define V14 0
             #define V15 0
             #define V16 0
             #define V17 0
             #define V18 0
             #define V19 0
             #define V20 0
             #endif
            #else
            #define V13 0
            #define V14 0
            #define V15 0
            #define V16 0
            #define V17 0
            #define V18 0
            #define V19 0
            #define V20 0
            #endif
           #else
           #define V12 0
           #define V13 0
           #define V14 0
           #define V15 0
           #define V16 0
           #define V17 0
           #define V18 0
           #define V19 0
           #define V20 0
           #endif
          #else
          #define V11 0
          #define V12 0
          #define V13 0
          #define V14 0
          #define V15 0
          #define V16 0
          #define V17 0
          #define V18 0
          #define V19 0
          #define V20 0
          #endif
         #else
         #define V10 0
         #define V11 0
         #define V12 0
         #define V13 0
         #define V14 0
         #define V15 0
         #define V16 0
         #define V17 0
         #define V18 0
         #define V19 0
         #define V20 0
         #endif
        #else
        #define V9 0
        #define V10 0
        #define V11 0
        #define V12 0
        #define V13 0
        #define V14 0
        #define V15 0
        #define V16 0
        #define V17 0
        #define V18 0
        #define V19 0
        #define V20 0
        #endif
       #else
       #define V8 0
       #define V9 0
       #define V10 0
       #define V11 0
       #define V12 0
       #define V13 0
       #define V14 0
       #define V15 0
       #define V16 0
       #define V17 0
       #define V18 0
       #define V19 0
       #define V20 0
       #endif
      #else
      #define V7 0
      #define V8 0
      #define V9 0
      #define V10 0
      #define V11 0
      #define V12 0
      #define V13 0
      #define V14 0
      #define V15 0
      #define V16 0
      #define V17 0
      #define V18 0
      #define V19 0
      #define V20 0
      #endif
     #else
     #define V6 0
     #define V7 0
     #define V8 0
     #define V9 0
     #define V10 0
     #define V11 0
     #define V12 0
     #define V13 0
     #define V14 0
     #define V15 0
     #define V16 0
     #define V17 0
     #define V18 0
     #define V19 0
     #define V20 0
     #endif
    #else
    #define V5 0
    #define V6 0
    #define V7 0
    #define V8 0
    #define V9 0
    #define V10 0
    #define V11 0
    #define V12 0
    #define V13 0
    #define V14 0
    #define V15 0
    #define V16 0
    #define V17 0
    #define V18 0
    #define V19 0
    #define V20 0
    #endif
   #else
   #define V4 0
   #define V5 0
   #define V6 0
   #define V7 0
   #define V8 0
   #define V9 0
   #define V10 0
   #define V11 0
   #define V12 0
   #define V13 0
   #define V14 0
   #define V15 0
   #define V16 0
   #define V17 0
   #define V18 0
   #define V19 0
   #define V20 0
   #endif
  #else
  #define V3 0
  #define V4 0
  #define V5 0
  #define V6 0
  #define V7 0
  #define V8 0
  #define V9 0
  #define V10 0
  #define V11 0
  #define V12 0
  #define V13 0
  #define V14 0
  #define V15 0
  #define V16 0
  #define V17 0
  #define V18 0
  #define V19 0
  #define V20 0
  #endif
 #else
 #define V2 0
 #define V3 0
 #define V4 0
 #define V5 0
 #define V6 0
 #define V7 0
 #define V8 0
 #define V9 0
 #define V10 0
 #define V11 0
 #define V12 0
 #define V13 0
 #define V14 0
 #define V15 0
 #define V16 0
 #define V17 0
 #define V18 0
 #define V19 0
 #define V20 0
 #endif
#else
#define V1 0
#define V2 0
#define V3 0
#define V4 0
#define V5 0
#define V6 0
#define V7 0
#define V8 0
#define V9 0
#define V10 0
#define V11 0
#define V12 0
#define V13 0
#define V14 0
#define V15 0
#define V16 0
#define V17 0
#define V18 0
#define V19 0
#define V20 0
#endif

int main(void) {
    return V1+V2+V3+V4+V5+V6+V7+V8+V9+V10+
           V11+V12+V13+V14+V15+V16+V17+V18+V19+V20;
}
