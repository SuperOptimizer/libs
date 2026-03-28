/* SPDX-License-Identifier: GPL-2.0 */
/* Stub win_minmax.h for free-cc kernel compilation testing */
#ifndef _LINUX_WIN_MINMAX_H
#define _LINUX_WIN_MINMAX_H

#include <linux/types.h>

struct minmax_sample {
    u32 t;
    u32 v;
};

struct minmax {
    struct minmax_sample s[3];
};

extern u32 minmax_running_max(struct minmax *m, u32 win, u32 t, u32 meas);
extern u32 minmax_running_min(struct minmax *m, u32 win, u32 t, u32 meas);

#endif /* _LINUX_WIN_MINMAX_H */
