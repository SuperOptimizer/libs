/* Kernel pattern: I/O memory mapped register access */
#include <linux/types.h>
#include <linux/io.h>
#include <linux/kernel.h>

struct hw_regs {
    volatile u32 control;
    volatile u32 status;
    volatile u32 data_in;
    volatile u32 data_out;
    volatile u32 interrupt_mask;
    volatile u32 interrupt_status;
};

struct hw_device {
    void __iomem *base;
    int irq;
    unsigned long phys_addr;
    unsigned int flags;
};

#define REG_CONTROL   0x00
#define REG_STATUS    0x04
#define REG_DATA_IN   0x08
#define REG_DATA_OUT  0x0C
#define REG_IRQ_MASK  0x10
#define REG_IRQ_STAT  0x14

#define CTRL_ENABLE   (1 << 0)
#define CTRL_RESET    (1 << 1)
#define CTRL_IRQ_EN   (1 << 2)

#define STATUS_BUSY   (1 << 0)
#define STATUS_DONE   (1 << 1)
#define STATUS_ERROR  (1 << 2)

static u32 hw_read_reg(struct hw_device *dev, unsigned int offset)
{
    return readl(dev->base + offset);
}

static void hw_write_reg(struct hw_device *dev, unsigned int offset, u32 val)
{
    writel(val, dev->base + offset);
}

static void hw_set_bits(struct hw_device *dev, unsigned int offset, u32 bits)
{
    u32 val = hw_read_reg(dev, offset);
    val |= bits;
    hw_write_reg(dev, offset, val);
}

static void hw_clear_bits(struct hw_device *dev, unsigned int offset, u32 bits)
{
    u32 val = hw_read_reg(dev, offset);
    val &= ~bits;
    hw_write_reg(dev, offset, val);
}

static int hw_wait_done(struct hw_device *dev, int timeout)
{
    int i;
    for (i = 0; i < timeout; i++) {
        u32 status = hw_read_reg(dev, REG_STATUS);
        if (status & STATUS_DONE)
            return 0;
        if (status & STATUS_ERROR)
            return -1;
    }
    return -2; /* timeout */
}

static int hw_init(struct hw_device *dev)
{
    hw_write_reg(dev, REG_CONTROL, CTRL_RESET);
    hw_write_reg(dev, REG_IRQ_MASK, 0);
    hw_write_reg(dev, REG_CONTROL, CTRL_ENABLE);
    return 0;
}

static int hw_transfer(struct hw_device *dev, u32 data)
{
    hw_write_reg(dev, REG_DATA_IN, data);
    hw_set_bits(dev, REG_CONTROL, CTRL_ENABLE);

    if (hw_wait_done(dev, 1000) < 0)
        return -1;

    return (int)hw_read_reg(dev, REG_DATA_OUT);
}

void test_iomem(void)
{
    struct hw_device dev;
    int result;

    dev.base = NULL;
    dev.irq = 42;
    dev.phys_addr = 0xFE000000UL;
    dev.flags = 0;

    (void)hw_init;
    (void)hw_transfer;
    (void)hw_clear_bits;
    (void)result;
}
