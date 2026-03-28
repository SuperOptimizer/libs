/* Kernel pattern: designated initializers and compound literals */
#include <linux/types.h>
#include <linux/kernel.h>

struct point {
    int x;
    int y;
    int z;
};

struct color {
    unsigned char r, g, b, a;
};

struct config_entry {
    const char *name;
    int value;
    unsigned long flags;
    struct point origin;
    struct color fg;
    struct color bg;
};

/* Table of configs using designated initializers */
static const struct config_entry default_configs[] = {
    {
        .name  = "display",
        .value = 1,
        .flags = 0x01,
        .origin = { .x = 0, .y = 0, .z = 0 },
        .fg = { .r = 255, .g = 255, .b = 255, .a = 255 },
        .bg = { .r = 0,   .g = 0,   .b = 0,   .a = 255 },
    },
    {
        .name  = "network",
        .value = 2,
        .flags = 0x03,
        .origin = { .x = 10, .y = 20, .z = 0 },
        .fg = { .r = 0, .g = 255, .b = 0, .a = 200 },
        .bg = { .r = 0, .g = 0, .b = 128, .a = 200 },
    },
    {
        .name  = "storage",
        .value = 3,
        .flags = 0x07,
        .origin = { .x = -1, .y = -1, .z = 0 },
        .fg = { .r = 255, .g = 0, .b = 0, .a = 128 },
        .bg = { .r = 64, .g = 64, .b = 64, .a = 128 },
    },
};

static const int num_configs = sizeof(default_configs) / sizeof(default_configs[0]);

/* Array with designated initializers at specific indices */
static const char *status_names[] = {
    [0] = "idle",
    [1] = "active",
    [2] = "suspended",
    [3] = "error",
    [7] = "unknown",
};

static const struct config_entry *find_config(const char *name)
{
    int i;
    for (i = 0; i < num_configs; i++) {
        if (strcmp(default_configs[i].name, name) == 0)
            return &default_configs[i];
    }
    return NULL;
}

static struct point add_points(struct point a, struct point b)
{
    struct point result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
    return result;
}

void test_designated_init(void)
{
    const struct config_entry *cfg;
    struct point sum;

    cfg = find_config("network");
    if (cfg) {
        sum = add_points(cfg->origin,
                         (struct point){ .x = 5, .y = 5, .z = 1 });
        (void)sum;
    }

    (void)status_names;
}
