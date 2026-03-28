/* SPDX-License-Identifier: GPL-2.0 */
/* Stub init.h for free-cc kernel compilation testing */
#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/types.h>

/* Section annotations */
#define __init
#define __exit
#define __initdata
#define __exitdata
#define __initconst
#define __exitcall(fn)

/* Setup and param macros */
#define __setup(str, fn)
#define __setup_param(str, unique_id, fn, early)
#define early_param(str, fn)

/* Initcall levels */
#define pure_initcall(fn)
#define core_initcall(fn)
#define core_initcall_sync(fn)
#define postcore_initcall(fn)
#define postcore_initcall_sync(fn)
#define arch_initcall(fn)
#define arch_initcall_sync(fn)
#define subsys_initcall(fn)
#define subsys_initcall_sync(fn)
#define fs_initcall(fn)
#define fs_initcall_sync(fn)
#define rootfs_initcall(fn)
#define device_initcall(fn)
#define device_initcall_sync(fn)
#define late_initcall(fn)
#define late_initcall_sync(fn)

/* Module init/exit */
#define module_init(fn)
#define module_exit(fn)

/* console_initcall */
#define console_initcall(fn)

/* Security initcall */
#define security_initcall(fn)

#endif /* _LINUX_INIT_H */
