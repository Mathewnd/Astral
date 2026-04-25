#ifndef _LIST_H
#define _LIST_H

#include <stddef.h>

typedef struct _list_node {
    struct _list_node *prev;
    struct _list_node *next;
} list_node_t;

typedef struct {
    list_node_t *head;
    list_node_t *tail;
} list_t;

#define LIST_INIT_VALUE ((list_t){.head = NULL, .tail = NULL})

static inline void list_init(list_t *list) {
    list->head = NULL;
    list->tail = NULL;
}

static inline void list_push_front(list_t *list, list_node_t *node) {
    node->prev = NULL;
    node->next = list->head;
    if (list->head != NULL) {
        list->head->prev = node;
    } else {
        list->tail = node;
    }
    list->head = node;
}

static inline void list_push_back(list_t *list, list_node_t *node) {
    node->next = NULL;
    node->prev = list->tail;
    if (list->tail != NULL) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
}

static inline void list_remove(list_t *list, list_node_t *node) {
    if (node->prev != NULL) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }
    if (node->next != NULL) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }
    node->prev = NULL;
    node->next = NULL;
}

static inline list_node_t* list_pop_front(list_t *list) {
    if (list->head == NULL) {
        return NULL;
    }
    list_node_t *node = list->head;
    list_remove(list, node);
    return node;
}

static inline list_node_t* list_pop_back(list_t *list) {
    if (list->tail == NULL) {
        return NULL;
    }
    list_node_t *node = list->tail;
    list_remove(list, node);
    return node;
}

#define list_for_each(LIST, ITER) \
    for (list_node_t *ITER = (LIST)->head; ITER != NULL; ITER = ITER->next)

#define list_for_each_safe(LIST, ITER) \
	for (list_node_t *ITER = (LIST)->head, *_next = (ITER ? ITER->next : NULL); ITER != NULL; ITER = _next, _next = (ITER ? ITER->next : NULL))

#endif
