/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STRING_H
#define _LINUX_STRING_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/compiler.h>

/* Standard string functions - declared here, defined in lib/string.c */
extern char *strcpy(char *, const char *);
extern char *strncpy(char *, const char *, size_t);
extern size_t strlcpy(char *, const char *, size_t);
extern size_t strscpy(char *, const char *, size_t);
extern char *strcat(char *, const char *);
extern char *strncat(char *, const char *, size_t);
extern size_t strlcat(char *, const char *, size_t);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, size_t);
extern int strcasecmp(const char *, const char *);
extern int strncasecmp(const char *, const char *, size_t);
extern char *strchr(const char *, int);
extern char *strchrnul(const char *, int);
extern char *strnchr(const char *, size_t, int);
extern char *strrchr(const char *, int);
extern char *skip_spaces(const char *);
extern char *strim(char *);
extern size_t strlen(const char *);
extern size_t strnlen(const char *, size_t);
extern char *strpbrk(const char *, const char *);
extern char *strsep(char **, const char *);
extern char *strstr(const char *, const char *);
extern char *strnstr(const char *, const char *, size_t);
extern char *strreplace(char *, char, char);

extern void *memset(void *, int, size_t);
extern void *memcpy(void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern void *memscan(void *, int, size_t);
extern int memcmp(const void *, const void *, size_t);
extern void *memchr(const void *, int, size_t);
extern void *memchr_inv(const void *, int, size_t);

extern void kfree_const(const void *);
extern char *kstrdup(const char *, gfp_t);
extern char *kstrndup(const char *, size_t, gfp_t);
extern void *kmemdup(const void *, size_t, gfp_t);
extern char *kmemdup_nul(const char *, size_t, gfp_t);

/* Fortify stubs */
#define __NO_FORTIFY
#define __underlying_memcpy memcpy
#define __underlying_memmove memmove
#define __underlying_memset memset
#define __underlying_memchr memchr
#define __underlying_strcpy strcpy
#define __underlying_strlen strlen
#define __underlying_strnlen strnlen
#define __underlying_strncpy strncpy

/* Wildcard matching */
extern int match_string(const char * const *array, size_t n, const char *string);

#endif /* _LINUX_STRING_H */
