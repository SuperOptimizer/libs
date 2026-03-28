/* Kernel pattern: simple hash table implementation */
#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/kernel.h>

#define HASH_BITS 8
#define HASH_SIZE (1 << HASH_BITS)

struct hash_entry {
    struct list_head node;
    unsigned long key;
    void *value;
};

struct hash_table {
    struct list_head buckets[HASH_SIZE];
    unsigned long count;
};

static unsigned long simple_hash(unsigned long key)
{
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = (key >> 16) ^ key;
    return key & (HASH_SIZE - 1);
}

static void hash_init_table(struct hash_table *ht)
{
    int i;
    for (i = 0; i < HASH_SIZE; i++)
        INIT_LIST_HEAD(&ht->buckets[i]);
    ht->count = 0;
}

static int hash_insert(struct hash_table *ht, unsigned long key, void *value)
{
    unsigned long bucket = simple_hash(key);
    struct hash_entry *entry;

    entry = kmalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry)
        return -1;

    entry->key = key;
    entry->value = value;
    list_add(&entry->node, &ht->buckets[bucket]);
    ht->count++;
    return 0;
}

static void *hash_lookup(struct hash_table *ht, unsigned long key)
{
    unsigned long bucket = simple_hash(key);
    struct hash_entry *entry;

    list_for_each_entry(entry, &ht->buckets[bucket], node) {
        if (entry->key == key)
            return entry->value;
    }
    return NULL;
}

static int hash_remove(struct hash_table *ht, unsigned long key)
{
    unsigned long bucket = simple_hash(key);
    struct hash_entry *entry;
    struct hash_entry *tmp;

    list_for_each_entry_safe(entry, tmp, &ht->buckets[bucket], node) {
        if (entry->key == key) {
            list_del(&entry->node);
            kfree(entry);
            ht->count--;
            return 0;
        }
    }
    return -1;
}

void test_hash_table(void)
{
    struct hash_table ht;
    int values[16];
    void *found;
    int i;

    hash_init_table(&ht);

    for (i = 0; i < 16; i++) {
        values[i] = i * 100;
        hash_insert(&ht, (unsigned long)i, &values[i]);
    }

    found = hash_lookup(&ht, 5);
    (void)found;

    hash_remove(&ht, 10);
}
