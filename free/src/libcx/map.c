/*
 * map.c - Hash map implementation (string keys, void* values).
 * Open addressing with linear probing, auto-resize at 75% load.
 * Part of libcx. Pure C89.
 */

#include "cx_map.h"
#include <stdlib.h>
#include <string.h>

#define CX_MAP_INIT_SIZE 16
#define CX_MAP_LOAD_FACTOR 75  /* percent */

static unsigned long map_hash(const char *key)
{
    unsigned long h = 2166136261UL;
    while (*key) {
        h ^= (unsigned char)*key++;
        h *= 16777619UL;
    }
    return h;
}

static int map_find_slot(cx_map_entry *entries, int size, const char *key)
{
    unsigned long h;
    int idx;

    h = map_hash(key);
    idx = (int)(h % (unsigned long)size);

    while (entries[idx].occupied) {
        if (strcmp(entries[idx].key, key) == 0) {
            return idx;
        }
        idx = (idx + 1) % size;
    }
    return idx;
}

static void map_resize(cx_map *m)
{
    cx_map_entry *old_entries;
    int old_size;
    int i;
    int new_size;

    old_entries = m->entries;
    old_size = m->size;
    new_size = old_size * 2;

    m->entries = (cx_map_entry *)calloc((size_t)new_size, sizeof(cx_map_entry));
    m->size = new_size;
    m->count = 0;

    for (i = 0; i < old_size; i++) {
        if (old_entries[i].occupied) {
            cx_map_set(m, old_entries[i].key, old_entries[i].val);
            free(old_entries[i].key);
        }
    }
    free(old_entries);
}

cx_map *cx_map_create(void)
{
    cx_map *m;
    m = (cx_map *)malloc(sizeof(cx_map));
    if (!m) return NULL;

    m->entries = (cx_map_entry *)calloc(CX_MAP_INIT_SIZE, sizeof(cx_map_entry));
    m->size = CX_MAP_INIT_SIZE;
    m->count = 0;
    return m;
}

void cx_map_set(cx_map *m, const char *key, void *val)
{
    int idx;
    size_t klen;

    /* Resize if above load factor */
    if (m->count * 100 >= m->size * CX_MAP_LOAD_FACTOR) {
        map_resize(m);
    }

    idx = map_find_slot(m->entries, m->size, key);

    if (m->entries[idx].occupied) {
        /* Update existing */
        m->entries[idx].val = val;
    } else {
        /* Insert new */
        klen = strlen(key) + 1;
        m->entries[idx].key = (char *)malloc(klen);
        memcpy(m->entries[idx].key, key, klen);
        m->entries[idx].val = val;
        m->entries[idx].occupied = 1;
        m->count++;
    }
}

void *cx_map_get(cx_map *m, const char *key)
{
    int idx;
    idx = map_find_slot(m->entries, m->size, key);
    if (m->entries[idx].occupied) {
        return m->entries[idx].val;
    }
    return NULL;
}

int cx_map_del(cx_map *m, const char *key)
{
    int idx;
    int next;

    idx = map_find_slot(m->entries, m->size, key);
    if (!m->entries[idx].occupied) {
        return 0;
    }

    free(m->entries[idx].key);
    m->entries[idx].key = NULL;
    m->entries[idx].val = NULL;
    m->entries[idx].occupied = 0;
    m->count--;

    /* Rehash following entries to fill the gap */
    next = (idx + 1) % m->size;
    while (m->entries[next].occupied) {
        char *rkey;
        void *rval;
        int new_idx;

        rkey = m->entries[next].key;
        rval = m->entries[next].val;
        m->entries[next].key = NULL;
        m->entries[next].val = NULL;
        m->entries[next].occupied = 0;
        m->count--;

        new_idx = map_find_slot(m->entries, m->size, rkey);
        m->entries[new_idx].key = rkey;
        m->entries[new_idx].val = rval;
        m->entries[new_idx].occupied = 1;
        m->count++;

        next = (next + 1) % m->size;
    }

    return 1;
}

int cx_map_count(cx_map *m)
{
    return m->count;
}

void cx_map_free(cx_map *m)
{
    int i;
    for (i = 0; i < m->size; i++) {
        if (m->entries[i].occupied) {
            free(m->entries[i].key);
        }
    }
    free(m->entries);
    free(m);
}
