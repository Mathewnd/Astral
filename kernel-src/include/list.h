#ifndef _LIST_H
#define _LIST_H

#include <stdbool.h>
#include <stddef.h>

typedef struct _list_node {
	struct _list_node *prev;
	struct _list_node *next;
} list_node_t;

typedef list_node_t list_t;

#define LIST_INIT_VALUE(NAME) ((list_t) {.prev = &(NAME), .next = &(NAME)})

static inline void list_init(list_t *list) {
	list->prev = list;
	list->next = list;
}

static inline bool list_is_empty(const list_t *list) {
	return list->next == list;
}

static inline list_node_t *list_get_first(const list_t *list) {
	return list_is_empty(list) ? NULL : list->next;
}

static inline list_node_t *list_get_last(const list_t *list) {
	return list_is_empty(list) ? NULL : list->prev;
}

static inline void list_push_front(list_t *list, list_node_t *node) {
	node->prev = list;
	node->next = list->next;
	list->next->prev = node;
	list->next = node;
}

static inline void list_push_back(list_t *list, list_node_t *node) {
	node->next = list;
	node->prev = list->prev;
	list->prev->next = node;
	list->prev = node;
}

static inline void list_remove(list_node_t *node) {
	node->prev->next = node->next;
	node->next->prev = node->prev;
	node->prev = NULL;
	node->next = NULL;
}

static inline list_node_t *list_pop_front(list_t *list) {
	list_node_t *node = list->next;
	if (node == list)
		return NULL;
	list_remove(node);
	return node;
}

static inline list_node_t *list_pop_back(list_t *list) {
	list_node_t *node = list->prev;
	if (node == list)
		return NULL;
	list_remove(node);
	return node;
}

#define list_for_each(LIST, ITER) \
	for (list_node_t *ITER = (LIST)->next; ITER != (LIST); ITER = ITER->next)

#define list_for_each_safe(LIST, ITER, NEXT) \
	for (list_node_t *ITER = (LIST)->next, *NEXT = ITER->next; \
		 ITER != (LIST); \
		 ITER = NEXT, NEXT = ITER->next)

#endif
