#ifndef _TRIE_H
#define _TRIE_H

#include <stddef.h>
#include <stdint.h>

#define TRIE_BITS 6
#define TRIE_NODE_COUNT (1 << TRIE_BITS)
#define TRIE_MAX_DEPTH (2 * ((64 + TRIE_BITS - 1) / TRIE_BITS) - 1)

typedef struct trie_node trie_node_t;
struct trie_node {
	uint8_t count;
	union {
		trie_node_t *nodes[TRIE_NODE_COUNT];
		void *items[TRIE_NODE_COUNT];
	};
};

typedef struct {
	size_t allocation_count;
	trie_node_t *allocations[TRIE_MAX_DEPTH];
} trie_preallocation_t;

typedef struct {
	uint8_t height;
	union {
		void *root_item;
		trie_node_t *root;
	};
} trie_t;

typedef void (trie_iterate_fn_t)(void *item);

void trie_init(trie_t *trie);
trie_preallocation_t *trie_preallocate(void);
int trie_insert(trie_t *trie, uint64_t key, void *value);
int trie_insert_preallocated(trie_t *trie, uint64_t key, void *value, trie_preallocation_t *preallocation);
void trie_free_preallocation(trie_preallocation_t *preallocation);
int trie_lookup(trie_t *trie, uint64_t key, void **ret);
int trie_remove(trie_t *trie, uint64_t key);
void trie_truncate(trie_t *trie, uint64_t max_key);
void trie_iterate(trie_t *trie, uint64_t min_key, uint64_t max_key, trie_iterate_fn_t);

#endif
