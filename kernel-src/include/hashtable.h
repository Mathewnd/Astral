#ifndef _HASHTABLE_H
#define _HASHTABLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct hashentry_t {
	struct hashentry_t *next;
	struct hashentry_t *prev;
	uintmax_t hash;
	size_t keysize;
	void *key;
	void *value;
} hashentry_t;

typedef struct {
	size_t capacity;
	hashentry_t **entries;
} hashtable_t;

int hashtable_init(hashtable_t *table, size_t size);
int hashtable_set(hashtable_t *table, void *value, void *key, size_t keysize, bool allocate);
int hashtable_get(hashtable_t *table, void **value, void *key, size_t keysize);
int hashtable_remove(hashtable_t *table, void *key, size_t keysize);
int hashtable_destroy(hashtable_t *table);

#define HASHTABLE_FOREACH(table) \
	for (uintmax_t toffset = 0; toffset < table->capacity; ++toffset) \
		for (hashentry_t *entry = table->entries[toffset]; entry != NULL; entry = entry->next)

#endif
