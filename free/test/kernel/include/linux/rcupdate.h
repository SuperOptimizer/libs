/* SPDX-License-Identifier: GPL-2.0 */
/* Stub rcupdate.h for free-cc kernel compilation testing */
#ifndef _LINUX_RCUPDATE_H
#define _LINUX_RCUPDATE_H

#define rcu_read_lock()   do {} while (0)
#define rcu_read_unlock() do {} while (0)
#define rcu_assign_pointer(p, v) ((p) = (v))
#define rcu_dereference(p) (p)
#define rcu_dereference_raw(p) (p)
#define rcu_dereference_protected(p, c) (p)
#define RCU_INIT_POINTER(p, v) ((p) = (v))
#define synchronize_rcu() do {} while (0)
#define call_rcu(head, func) do {} while (0)
#define rcu_barrier() do {} while (0)

#endif /* _LINUX_RCUPDATE_H */
