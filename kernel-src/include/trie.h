#ifndef _TRIE_H
#define _TRIE_H

#include <stdint.h>

#define TRIE_BITS 6
#define TRIE_NODE_COUNT (1 << TRIE_BITS)

typedef struct trie_node trie_node_t;
struct trie_node {
	uint8_t count;
	union {
		trie_node_t *nodes[TRIE_NODE_COUNT];
		void *items[TRIE_NODE_COUNT];
	};
};

typedef struct {
	uint8_t height;
	union {
		void *root_item;
		trie_node_t *root;
	};
} trie_t;

typedef void (trie_iterate_fn_t)(void *item);

void trie_init(trie_t *trie);
int trie_insert(trie_t *trie, uint64_t key, void *value);
int trie_lookup(trie_t *trie, uint64_t key, void **ret);
int trie_remove(trie_t *trie, uint64_t key);
void trie_truncate(trie_t *trie, uint64_t max_key);
void trie_iterate(trie_t *trie, uint64_t min_key, uint64_t max_key, trie_iterate_fn_t);

#endif
