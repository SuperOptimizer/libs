/*
 * cx_list.h - Intrusive doubly-linked list macros (header-only).
 * Part of libcx. Pure C89.
 *
 * Usage:
 *   struct my_node {
 *       int value;
 *       cx_list_node link;
 *   };
 *   cx_list_head list;
 *   CX_LIST_INIT(&list);
 *   struct my_node n;
 *   n.value = 42;
 *   CX_LIST_PUSH(&list, &n.link);
 */

#ifndef CX_LIST_H
#define CX_LIST_H

#include <stddef.h>

typedef struct cx_list_node {
    struct cx_list_node *next;
    struct cx_list_node *prev;
} cx_list_node;

typedef struct {
    cx_list_node head;
} cx_list_head;

/* Get the struct that contains this list node */
#define CX_LIST_ENTRY(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* Initialize a list head */
#define CX_LIST_INIT(list) do { \
    (list)->head.next = &(list)->head; \
    (list)->head.prev = &(list)->head; \
} while(0)

/* Check if list is empty */
#define CX_LIST_EMPTY(list) \
    ((list)->head.next == &(list)->head)

/* Push a node to the front of the list */
#define CX_LIST_PUSH(list, node) do { \
    cx_list_node *_n = (node); \
    cx_list_node *_h = &(list)->head; \
    _n->next = _h->next; \
    _n->prev = _h; \
    _h->next->prev = _n; \
    _h->next = _n; \
} while(0)

/* Push a node to the back of the list */
#define CX_LIST_PUSH_BACK(list, node) do { \
    cx_list_node *_n = (node); \
    cx_list_node *_h = &(list)->head; \
    _n->next = _h; \
    _n->prev = _h->prev; \
    _h->prev->next = _n; \
    _h->prev = _n; \
} while(0)

/* Remove a node from its list */
#define CX_LIST_REMOVE(node) do { \
    cx_list_node *_n = (node); \
    _n->prev->next = _n->next; \
    _n->next->prev = _n->prev; \
    _n->next = _n; \
    _n->prev = _n; \
} while(0)

/* Pop the first node from the list (returns cx_list_node*, or NULL if empty) */
#define CX_LIST_POP(list) \
    (CX_LIST_EMPTY(list) ? (cx_list_node *)0 : \
     ((list)->head.next->prev = (list)->head.next->next->prev, \
      (list)->head.next->next->prev = &(list)->head, \
      (list)->head.next->prev->prev)) \

/* Iterate over all entries in the list.
 * pos is a cx_list_node*, tmp is a cx_list_node* for safe deletion. */
#define CX_LIST_FOREACH(pos, list) \
    for ((pos) = (list)->head.next; \
         (pos) != &(list)->head; \
         (pos) = (pos)->next)

/* Safe iteration (allows removal during iteration) */
#define CX_LIST_FOREACH_SAFE(pos, tmp, list) \
    for ((pos) = (list)->head.next, (tmp) = (pos)->next; \
         (pos) != &(list)->head; \
         (pos) = (tmp), (tmp) = (pos)->next)

#endif
