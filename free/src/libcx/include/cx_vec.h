/*
 * cx_vec.h - Type-safe dynamic array via macros.
 * Part of libcx. Pure C89.
 *
 * Usage:
 *   CX_VEC_DEFINE(int_vec, int)
 *   int_vec v = int_vec_new();
 *   int_vec_push(&v, 42);
 *   int x = int_vec_get(&v, 0);
 *   int_vec_free(&v);
 */

#ifndef CX_VEC_H
#define CX_VEC_H

#include <stdlib.h>
#include <string.h>

#define CX_VEC_DEFINE(name, type) \
    typedef struct { \
        type *data; \
        int len; \
        int cap; \
    } name; \
    \
    static name name##_new(void) \
    { \
        name v; \
        v.data = NULL; \
        v.len = 0; \
        v.cap = 0; \
        return v; \
    } \
    \
    static void name##_grow(name *v) \
    { \
        int newcap; \
        type *newdata; \
        newcap = v->cap == 0 ? 8 : v->cap * 2; \
        newdata = (type *)malloc((size_t)newcap * sizeof(type)); \
        if (v->data) { \
            memcpy(newdata, v->data, (size_t)v->len * sizeof(type)); \
            free(v->data); \
        } \
        v->data = newdata; \
        v->cap = newcap; \
    } \
    \
    static void name##_push(name *v, type val) \
    { \
        if (v->len >= v->cap) { \
            name##_grow(v); \
        } \
        v->data[v->len++] = val; \
    } \
    \
    static type name##_pop(name *v) \
    { \
        return v->data[--v->len]; \
    } \
    \
    static type name##_get(name *v, int idx) \
    { \
        return v->data[idx]; \
    } \
    \
    static void name##_set(name *v, int idx, type val) \
    { \
        v->data[idx] = val; \
    } \
    \
    static int name##_len(name *v) \
    { \
        return v->len; \
    } \
    \
    static int name##_cap(name *v) \
    { \
        return v->cap; \
    } \
    \
    static void name##_free(name *v) \
    { \
        free(v->data); \
        v->data = NULL; \
        v->len = 0; \
        v->cap = 0; \
    }

#endif
