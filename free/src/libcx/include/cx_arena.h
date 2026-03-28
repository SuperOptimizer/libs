/*
 * cx_arena.h - Arena allocator for bulk allocation with fast reset.
 * Part of libcx. Pure C89.
 */

#ifndef CX_ARENA_H
#define CX_ARENA_H

#include <stddef.h>

typedef struct cx_arena_chunk {
    struct cx_arena_chunk *next;
    size_t size;
    size_t used;
    char data[1]; /* flexible tail */
} cx_arena_chunk;

typedef struct {
    cx_arena_chunk *head;
    cx_arena_chunk *current;
    size_t default_size;
} cx_arena;

cx_arena *cx_arena_create(size_t size);
void     *cx_arena_alloc(cx_arena *a, size_t size);
void      cx_arena_reset(cx_arena *a);
void      cx_arena_destroy(cx_arena *a);

#endif
