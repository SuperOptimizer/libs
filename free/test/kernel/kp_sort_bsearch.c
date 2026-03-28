/* Kernel pattern: sort and binary search */
#include <linux/types.h>
#include <linux/sort.h>
#include <linux/bsearch.h>
#include <linux/kernel.h>

struct record {
    unsigned long key;
    int value;
    char tag[8];
};

static int cmp_records(const void *a, const void *b)
{
    const struct record *ra = a;
    const struct record *rb = b;
    if (ra->key < rb->key) return -1;
    if (ra->key > rb->key) return 1;
    return 0;
}

static int cmp_ulong(const void *key, const void *elem)
{
    unsigned long k = *(const unsigned long *)key;
    const struct record *r = elem;
    if (k < r->key) return -1;
    if (k > r->key) return 1;
    return 0;
}

static struct record *find_record(struct record *arr, int count,
                                  unsigned long key)
{
    return bsearch(&key, arr, count, sizeof(*arr), cmp_ulong);
}

static void swap_records(void *a, void *b, int size)
{
    char tmp[sizeof(struct record)];
    (void)size;
    memcpy(tmp, a, sizeof(struct record));
    memcpy(a, b, sizeof(struct record));
    memcpy(b, tmp, sizeof(struct record));
}

void test_sort_bsearch(void)
{
    struct record data[8];
    struct record *found;
    int i;
    unsigned long keys[] = { 50, 20, 80, 10, 60, 30, 70, 40 };

    for (i = 0; i < 8; i++) {
        data[i].key = keys[i];
        data[i].value = i;
        data[i].tag[0] = 'A' + (char)i;
        data[i].tag[1] = '\0';
    }

    sort(data, 8, sizeof(struct record), cmp_records, swap_records);

    found = find_record(data, 8, 60);
    (void)found;

    found = find_record(data, 8, 99);
    (void)found;
}
