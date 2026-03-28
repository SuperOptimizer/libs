#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF    (-1)
#define BUFSIZ 4096

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* stdin=0, stdout=1, stderr=2 as fd numbers */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

typedef struct {
    int    fd;
    int    flags;
    int    eof;
    int    error;
    char   buf[BUFSIZ];
    size_t buf_pos;
    size_t buf_len;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE   *fopen(const char *path, const char *mode);
int     fclose(FILE *f);
size_t  fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t  fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int     fseek(FILE *f, long offset, int whence);
long    ftell(FILE *f);
int     fflush(FILE *f);
int     feof(FILE *f);
int     ferror(FILE *f);
int     fgetc(FILE *f);
int     fputc(int c, FILE *f);
char   *fgets(char *s, int size, FILE *f);
int     fputs(const char *s, FILE *f);

FILE   *tmpfile(void);
void    rewind(FILE *f);

int     printf(const char *fmt, ...);
int     fprintf(FILE *f, const char *fmt, ...);
int     sprintf(char *str, const char *fmt, ...);
int     snprintf(char *str, size_t size, const char *fmt, ...);
int     vfprintf(FILE *f, const char *fmt, va_list ap);
int     vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

int     puts(const char *s);
int     putchar(int c);
int     getchar(void);

void    perror(const char *s);

/* remove/rename */
int     remove(const char *path);
int     rename(const char *oldpath, const char *newpath);

#endif
