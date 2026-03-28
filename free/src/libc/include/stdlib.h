#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647

int     rand(void);
void    srand(unsigned int seed);

void   *malloc(size_t size);
void   *calloc(size_t nmemb, size_t size);
void   *realloc(void *ptr, size_t size);
void    free(void *ptr);

void    exit(int status);
void    abort(void);

int     atoi(const char *s);
long    atol(const char *s);
long    strtol(const char *s, char **endp, int base);
unsigned long strtoul(const char *s, char **endp, int base);
double  strtod(const char *s, char **endp);

void    qsort(void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *));

char   *getenv(const char *name);

#endif
