/* SPDX-License-Identifier: GPL-2.0 */
/* Stub asm/io.h for free-cc kernel compilation testing */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/types.h>

#define __iomem

static inline u8 __raw_readb(const volatile void __iomem *addr)
{
    return *(const volatile u8 *)addr;
}

static inline u16 __raw_readw(const volatile void __iomem *addr)
{
    return *(const volatile u16 *)addr;
}

static inline u32 __raw_readl(const volatile void __iomem *addr)
{
    return *(const volatile u32 *)addr;
}

static inline u64 __raw_readq(const volatile void __iomem *addr)
{
    return *(const volatile u64 *)addr;
}

static inline void __raw_writeb(u8 val, volatile void __iomem *addr)
{
    *(volatile u8 *)addr = val;
}

static inline void __raw_writew(u16 val, volatile void __iomem *addr)
{
    *(volatile u16 *)addr = val;
}

static inline void __raw_writel(u32 val, volatile void __iomem *addr)
{
    *(volatile u32 *)addr = val;
}

static inline void __raw_writeq(u64 val, volatile void __iomem *addr)
{
    *(volatile u64 *)addr = val;
}

#define readb(addr) __raw_readb(addr)
#define readw(addr) __raw_readw(addr)
#define readl(addr) __raw_readl(addr)
#define readq(addr) __raw_readq(addr)
#define writeb(val, addr) __raw_writeb(val, addr)
#define writew(val, addr) __raw_writew(val, addr)
#define writel(val, addr) __raw_writel(val, addr)
#define writeq(val, addr) __raw_writeq(val, addr)

#define readb_relaxed readb
#define readw_relaxed readw
#define readl_relaxed readl
#define readq_relaxed readq
#define writeb_relaxed writeb
#define writew_relaxed writew
#define writel_relaxed writel
#define writeq_relaxed writeq

extern void __iomem *ioremap(phys_addr_t phys_addr, size_t size);
extern void iounmap(volatile void __iomem *addr);
extern void memcpy_fromio(void *dst, const volatile void __iomem *src, size_t count);
extern void memcpy_toio(volatile void __iomem *dst, const void *src, size_t count);
extern void memset_io(volatile void __iomem *addr, int value, size_t size);

#endif /* _ASM_IO_H */
