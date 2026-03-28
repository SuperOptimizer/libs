/* Kernel pattern: rwlock protected data structure */
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/kernel.h>

struct config_entry {
    struct list_head link;
    char key[32];
    char value[128];
};

struct config_store {
    rwlock_t lock;
    struct list_head entries;
    int count;
};

static void config_store_init(struct config_store *store)
{
    rwlock_init(&store->lock);
    INIT_LIST_HEAD(&store->entries);
    store->count = 0;
}

static const char *config_get(struct config_store *store, const char *key)
{
    struct config_entry *entry;
    const char *result = NULL;

    read_lock(&store->lock);
    list_for_each_entry(entry, &store->entries, link) {
        if (strcmp(entry->key, key) == 0) {
            result = entry->value;
            break;
        }
    }
    read_unlock(&store->lock);
    return result;
}

static void config_set(struct config_store *store,
                       struct config_entry *new_entry)
{
    write_lock(&store->lock);
    list_add(&new_entry->link, &store->entries);
    store->count++;
    write_unlock(&store->lock);
}

static int config_count(struct config_store *store)
{
    int count;
    read_lock(&store->lock);
    count = store->count;
    read_unlock(&store->lock);
    return count;
}

void test_rwlock(void)
{
    struct config_store store;
    struct config_entry e1, e2;
    const char *val;
    int cnt;

    config_store_init(&store);

    strcpy(e1.key, "debug_level");
    strcpy(e1.value, "3");
    config_set(&store, &e1);

    strcpy(e2.key, "log_file");
    strcpy(e2.value, "/var/log/kernel.log");
    config_set(&store, &e2);

    val = config_get(&store, "debug_level");
    (void)val;

    cnt = config_count(&store);
    (void)cnt;
}
