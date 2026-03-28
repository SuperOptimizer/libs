/* SPDX-License-Identifier: GPL-2.0 */
/* Stub cpu.h for free-cc kernel compilation testing */
#ifndef _LINUX_CPU_H
#define _LINUX_CPU_H

#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/cpumask.h>

struct device;

struct cpu {
    int node_id;
    int hotpluggable;
    struct device dev;
};

extern void cpu_maps_update_begin(void);
extern void cpu_maps_update_done(void);

#define cpus_read_lock() do {} while (0)
#define cpus_read_unlock() do {} while (0)

#define cpuhp_setup_state(state, name, startup, teardown) 0
#define cpuhp_setup_state_nocalls(state, name, startup, teardown) 0
#define cpuhp_remove_state(state) do {} while (0)
#define cpuhp_remove_state_nocalls(state) do {} while (0)

enum cpuhp_state {
    CPUHP_INVALID = -1,
    CPUHP_OFFLINE = 0,
    CPUHP_ONLINE
};

#endif /* _LINUX_CPU_H */
