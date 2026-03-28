/* SPDX-License-Identifier: GPL-2.0 */
/* Stub device.h for free-cc kernel compilation testing */
#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <linux/types.h>
#include <linux/compiler.h>

struct device;
struct device_attribute;

/* dev_printk stubs */
#define dev_emerg(dev, fmt, ...)  ((void)(dev), printk(fmt, ##__VA_ARGS__))
#define dev_alert(dev, fmt, ...)  ((void)(dev), printk(fmt, ##__VA_ARGS__))
#define dev_crit(dev, fmt, ...)   ((void)(dev), printk(fmt, ##__VA_ARGS__))
#define dev_err(dev, fmt, ...)    ((void)(dev), printk(fmt, ##__VA_ARGS__))
#define dev_warn(dev, fmt, ...)   ((void)(dev), printk(fmt, ##__VA_ARGS__))
#define dev_notice(dev, fmt, ...) ((void)(dev), printk(fmt, ##__VA_ARGS__))
#define dev_info(dev, fmt, ...)   ((void)(dev), printk(fmt, ##__VA_ARGS__))
#define dev_dbg(dev, fmt, ...)    do {} while (0)

extern const char *dev_name(const struct device *dev);

#endif /* _DEVICE_H_ */
