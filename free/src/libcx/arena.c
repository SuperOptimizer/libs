/*
 * arena.c - Arena allocator implementation.
 * Part of libcx. Pure C89.
 */

#include "cx_arena.h"
#include <stdlib.h>
#include <string.h>

#define CX_ARENA_MIN_SIZE 4096
#define CX_ARENA_ALIGN 8

static size_t align_up(size_t x, size_t a)
{
    return (x + a - 1) & ~(a - 1);
}

static cx_arena_chunk *chunk_new(size_t size)
{
    cx_arena_chunk *c;
    c = (cx_arena_chunk *)malloc(sizeof(cx_arena_chunk) - 1 + size);
    if (!c) return NULL;
    c->next = NULL;
    c->size = size;
    c->used = 0;
    return c;
}

cx_arena *cx_arena_create(size_t size)
{
    cx_arena *a;
    cx_arena_chunk *c;

    if (size < CX_ARENA_MIN_SIZE) {
        size = CX_ARENA_MIN_SIZE;
    }

    a = (cx_arena *)malloc(sizeof(cx_arena));
    if (!a) return NULL;

    c = chunk_new(size);
    if (!c) {
        free(a);
        return NULL;
    }

    a->head = c;
    a->current = c;
    a->default_size = size;
    return a;
}

void *cx_arena_alloc(cx_arena *a, size_t size)
{
    cx_arena_chunk *c;
    size_t aligned;
    size_t chunk_size;
    void *ptr;

    size = align_up(size, CX_ARENA_ALIGN);
    c = a->current;

    if (c->used + size <= c->size) {
        ptr = c->data + c->used;
        c->used += size;
        return ptr;
    }

    /* Need a new chunk */
    chunk_size = a->default_size;
    if (size > chunk_size) {
        chunk_size = size;
    }

    aligned = align_up(chunk_size, CX_ARENA_ALIGN);
    c = chunk_new(aligned);
    if (!c) return NULL;

    c->next = NULL;
    a->current->next = c;
    a->current = c;

    ptr = c->data + c->used;
    c->used += size;
    return ptr;
}

void cx_arena_reset(cx_arena *a)
{
    cx_arena_chunk *c;
    /* Free all chunks except the first */
    c = a->head->next;
    while (c) {
        cx_arena_chunk *next = c->next;
        free(c);
        c = next;
    }
    a->head->next = NULL;
    a->head->used = 0;
    a->current = a->head;
}

void cx_arena_destroy(cx_arena *a)
{
    cx_arena_chunk *c;
    cx_arena_chunk *next;

    c = a->head;
    while (c) {
        next = c->next;
        free(c);
        c = next;
    }
    free(a);
}
