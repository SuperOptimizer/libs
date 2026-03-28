/* SPDX-License-Identifier: GPL-2.0 */
/* Stub kstrtox.h for free-cc kernel compilation testing */
#ifndef _LINUX_KSTRTOX_H
#define _LINUX_KSTRTOX_H

#include <linux/types.h>

int kstrtoull(const char *s, unsigned int base, unsigned long long *res);
int kstrtoll(const char *s, unsigned int base, long long *res);
int kstrtoul(const char *s, unsigned int base, unsigned long *res);
int kstrtol(const char *s, unsigned int base, long *res);
int kstrtouint(const char *s, unsigned int base, unsigned int *res);
int kstrtoint(const char *s, unsigned int base, int *res);
int kstrtou16(const char *s, unsigned int base, u16 *res);
int kstrtos16(const char *s, unsigned int base, s16 *res);
int kstrtou8(const char *s, unsigned int base, u8 *res);
int kstrtos8(const char *s, unsigned int base, s8 *res);
int kstrtobool(const char *s, bool *res);

#define kstrtol_from_user    kstrtol_from_user
#define kstrtoul_from_user   kstrtoul_from_user

#endif /* _LINUX_KSTRTOX_H */
