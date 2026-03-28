/*
 * cx_map.h - Hash map with string keys and void* values.
 * Open addressing with linear probing, auto-resize at 75% load.
 * Part of libcx. Pure C89.
 */

#ifndef CX_MAP_H
#define CX_MAP_H

#include <stddef.h>

typedef struct {
    char *key;
    void *val;
    int occupied;
} cx_map_entry;

typedef struct {
    cx_map_entry *entries;
    int size;    /* number of slots */
    int count;   /* number of occupied entries */
} cx_map;

cx_map *cx_map_create(void);
void    cx_map_set(cx_map *m, const char *key, void *val);
void   *cx_map_get(cx_map *m, const char *key);
int     cx_map_del(cx_map *m, const char *key);
int     cx_map_count(cx_map *m);
void    cx_map_free(cx_map *m);

#endif
