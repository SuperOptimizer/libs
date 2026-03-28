/* Kernel pattern: bitmap operations */
#include <linux/types.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/kernel.h>

#define MY_BITMAP_SIZE 256

static DECLARE_BITMAP(resource_map, MY_BITMAP_SIZE);

static int allocate_resource(void)
{
    int bit = find_first_zero_bit(resource_map, MY_BITMAP_SIZE);
    if (bit >= MY_BITMAP_SIZE)
        return -1;
    set_bit(bit, resource_map);
    return bit;
}

static void free_resource(int bit)
{
    if (bit >= 0 && bit < MY_BITMAP_SIZE)
        clear_bit(bit, resource_map);
}

static int count_free_resources(void)
{
    return MY_BITMAP_SIZE - bitmap_weight(resource_map, MY_BITMAP_SIZE);
}

static void init_resource_map(void)
{
    bitmap_zero(resource_map, MY_BITMAP_SIZE);
}

void test_bitmap(void)
{
    int r1, r2, r3;
    int free_count;

    init_resource_map();

    r1 = allocate_resource();
    r2 = allocate_resource();
    r3 = allocate_resource();

    free_count = count_free_resources();
    (void)free_count;

    free_resource(r2);

    free_count = count_free_resources();
    (void)free_count;

    (void)r1;
    (void)r3;
}
