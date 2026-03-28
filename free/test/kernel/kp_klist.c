/* Kernel pattern: klist usage */
#include <linux/types.h>
#include <linux/klist.h>
#include <linux/kernel.h>

struct my_bus {
    struct klist devices;
    const char *name;
};

struct my_hw_device {
    struct klist_node node;
    int device_id;
    const char *name;
    unsigned int irq;
};

static void device_get(struct klist_node *n)
{
    (void)n;
}

static void device_put(struct klist_node *n)
{
    (void)n;
}

static void bus_init(struct my_bus *bus, const char *name)
{
    klist_init(&bus->devices, device_get, device_put);
    bus->name = name;
}

static void bus_add_device(struct my_bus *bus, struct my_hw_device *dev)
{
    klist_add_tail(&dev->node, &bus->devices);
}

static void bus_remove_device(struct my_hw_device *dev)
{
    klist_del(&dev->node);
}

void test_klist(void)
{
    struct my_bus pci_bus;
    struct my_hw_device devs[3];
    int i;

    bus_init(&pci_bus, "pci");

    for (i = 0; i < 3; i++) {
        devs[i].device_id = i + 1;
        devs[i].name = "device";
        devs[i].irq = 32 + (unsigned int)i;
        bus_add_device(&pci_bus, &devs[i]);
    }

    bus_remove_device(&devs[1]);
}
