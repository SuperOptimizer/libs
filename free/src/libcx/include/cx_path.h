/*
 * cx_path.h - Path manipulation utilities.
 * Part of libcx. Pure C89.
 *
 * All functions that return char* return malloc'd strings
 * that the caller must free.
 */

#ifndef CX_PATH_H
#define CX_PATH_H

char *cx_path_join(const char *a, const char *b);
char *cx_path_dirname(const char *path);
char *cx_path_basename(const char *path);
const char *cx_path_ext(const char *path);
char *cx_path_normalize(const char *path);
int   cx_path_is_abs(const char *path);

#endif
