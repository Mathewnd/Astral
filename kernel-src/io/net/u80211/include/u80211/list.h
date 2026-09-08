#ifndef U80211_LIST_H
#define U80211_LIST_H

#include <stddef.h>

typedef struct u80211_list_node {
	struct u80211_list_node *prev;
	struct u80211_list_node *next;
} u80211_list_node_t;

typedef struct {
	u80211_list_node_t *head;
	u80211_list_node_t *tail;
} u80211_list_t;

#define U80211_LIST_INIT_VALUE ((u80211_list_t) {.head = NULL, .tail = NULL})

static inline void u80211_list_init(u80211_list_t *list) {
	list->head = NULL;
	list->tail = NULL;
}

static inline void u80211_list_push_front(u80211_list_t *list, u80211_list_node_t *node) {
	node->prev = NULL;
	node->next = list->head;
	if (list->head != NULL) {
		list->head->prev = node;
	} else {
		list->tail = node;
	}
	list->head = node;
}

static inline void u80211_list_push_back(u80211_list_t *list, u80211_list_node_t *node) {
	node->next = NULL;
	node->prev = list->tail;
	if (list->tail != NULL) {
		list->tail->next = node;
	} else {
		list->head = node;
	}
	list->tail = node;
}

static inline void u80211_list_remove(u80211_list_t *list, u80211_list_node_t *node) {
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

static inline u80211_list_node_t *u80211_list_pop_front(u80211_list_t *list) {
	if (list->head == NULL) {
		return NULL;
	}
	u80211_list_node_t *node = list->head;
	u80211_list_remove(list, node);
	return node;
}

static inline u80211_list_node_t *u80211_list_pop_back(u80211_list_t *list) {
	if (list->tail == NULL) {
		return NULL;
	}
	u80211_list_node_t *node = list->tail;
	u80211_list_remove(list, node);
	return node;
}

#define u80211_list_for_each(LIST, ITER) \
	for (u80211_list_node_t *ITER = (LIST)->head; ITER != NULL; ITER = ITER->next)

#define u80211_list_for_each_safe(LIST, ITER) \
	for (u80211_list_node_t *ITER = (LIST)->head, *_next = (ITER ? ITER->next : NULL); ITER != NULL; ITER = _next, _next = (ITER ? ITER->next : NULL))

#endif
