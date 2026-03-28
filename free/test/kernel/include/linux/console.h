/* Stub console.h for free-cc kernel compilation testing */
#ifndef _LINUX_CONSOLE_H
#define _LINUX_CONSOLE_H

#include <linux/types.h>
#include <linux/printk.h>

extern void console_flush_on_panic(int mode);
#define CONSOLE_FLUSH_PENDING 0
#define CONSOLE_REPLAY_ALL    1

#endif
